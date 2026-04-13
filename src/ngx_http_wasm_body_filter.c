#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <ngx_http_wasm_module_int.h>

static ngx_http_output_body_filter_pt ngx_http_wasm_next_body_filter;

static ngx_int_t
ngx_http_wasm_body_filter_run_chunk(ngx_http_request_t *r,
                                    ngx_http_wasm_conf_t *wcf,
                                    ngx_http_wasm_main_conf_t *wmcf,
                                    ngx_buf_t *buf,
                                    const u_char *data,
                                    size_t len,
                                    ngx_uint_t eof,
                                    ngx_buf_t **out);
static ngx_buf_t *ngx_http_wasm_body_filter_create_buf(ngx_http_request_t *r,
                                                       ngx_str_t *chunk,
                                                       ngx_buf_t *src);
static ngx_int_t ngx_http_wasm_body_filter_append(ngx_http_request_t *r,
                                                  ngx_chain_t **head,
                                                  ngx_chain_t **tail,
                                                  ngx_buf_t *buf);

ngx_int_t ngx_http_wasm_body_filter_init_process(void) {
    ngx_http_wasm_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_wasm_body_filter_handler;

    return NGX_OK;
}

ngx_int_t ngx_http_wasm_body_filter_handler(ngx_http_request_t *r,
                                            ngx_chain_t *in) {
    static ngx_str_t phase_name = ngx_string("body filter");
    ngx_http_wasm_conf_t *wcf;
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_chain_t *cl;
    ngx_chain_t *out;
    ngx_chain_t *tail;
    ngx_buf_t *buf;
    ngx_buf_t *replacement;
    size_t len;
    ngx_uint_t eof;

    if (in == NULL) {
        return ngx_http_wasm_next_body_filter(r, in);
    }

    wcf = ngx_http_get_module_loc_conf(r, ngx_http_wasm_module);
    wmcf = ngx_http_get_module_main_conf(r, ngx_http_wasm_module);

    if (wcf == NULL || wcf->body_filter.set != 1 || wmcf == NULL ||
        wmcf->runtime == NULL || r->header_only) {
        return ngx_http_wasm_next_body_filter(r, in);
    }

    ngx_log_error(NGX_LOG_NOTICE,
                  r->connection->log,
                  0,
                  "ngx_wasm: %V handler module=\"%V\" export=\"%V\"",
                  &phase_name,
                  &wcf->body_filter.module_path,
                  &wcf->body_filter.export_name);

    out = NULL;
    tail = NULL;

    for (cl = in; cl != NULL; cl = cl->next) {
        buf = cl->buf;
        eof = buf->last_buf || buf->last_in_chain;

        if (ngx_buf_in_memory(buf)) {
            len = (size_t)(buf->last - buf->pos);

            if (len != 0 || eof) {
                if (ngx_http_wasm_body_filter_run_chunk(
                        r, wcf, wmcf, buf, buf->pos, len, eof, &replacement) !=
                    NGX_OK) {
                    return NGX_ERROR;
                }

                if (replacement != NULL) {
                    if (ngx_http_wasm_body_filter_append(
                            r, &out, &tail, replacement) != NGX_OK) {
                        return NGX_ERROR;
                    }

                    continue;
                }
            }

            if (ngx_http_wasm_body_filter_append(r, &out, &tail, buf) !=
                NGX_OK) {
                return NGX_ERROR;
            }

            continue;
        }

        /*
         * The first implementation only transforms in-memory chunks. File
         * buffers pass through unchanged, except an empty EOF chunk can still
         * be synthesized by a later filter stage and handled here.
         */
        if (ngx_buf_special(buf) && eof) {
            if (ngx_http_wasm_body_filter_run_chunk(
                    r, wcf, wmcf, buf, (u_char *)"", 0, eof, &replacement) !=
                NGX_OK) {
                return NGX_ERROR;
            }

            if (replacement != NULL) {
                if (ngx_http_wasm_body_filter_append(
                        r, &out, &tail, replacement) != NGX_OK) {
                    return NGX_ERROR;
                }

                continue;
            }
        }

        if (ngx_http_wasm_body_filter_append(r, &out, &tail, buf) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return ngx_http_wasm_next_body_filter(r, out);
}

static ngx_int_t
ngx_http_wasm_body_filter_run_chunk(ngx_http_request_t *r,
                                    ngx_http_wasm_conf_t *wcf,
                                    ngx_http_wasm_main_conf_t *wmcf,
                                    ngx_buf_t *buf,
                                    const u_char *data,
                                    size_t len,
                                    ngx_uint_t eof,
                                    ngx_buf_t **out) {
    ngx_http_wasm_exec_ctx_t exec;
    ngx_int_t rc;

    *out = NULL;

    ngx_http_wasm_runtime_init_exec_ctx(&exec,
                                        r,
                                        &wcf->body_filter,
                                        NGX_HTTP_WASM_PHASE_BODY_FILTER,
                                        wmcf->runtime);
    exec.fuel_limit = wcf->fuel_limit;
    exec.timeslice_fuel = wcf->timeslice_fuel;
    exec.fuel_remaining = wcf->fuel_limit;

    rc = ngx_http_wasm_abi_resp_set_body_chunk_input(&exec.abi, data, len, eof);
    if (rc != NGX_HTTP_WASM_OK) {
        ngx_http_wasm_runtime_cleanup_exec_ctx(&exec);
        return NGX_ERROR;
    }

    rc = ngx_http_wasm_runtime_run(&exec);
    if (rc != NGX_OK) {
        ngx_http_wasm_runtime_cleanup_exec_ctx(&exec);
        return NGX_ERROR;
    }

    if (exec.abi.resp_body_chunk_output_set) {
        *out = ngx_http_wasm_body_filter_create_buf(
            r, &exec.abi.resp_body_chunk_output, buf);
        ngx_http_wasm_runtime_cleanup_exec_ctx(&exec);

        if (*out == NULL) {
            return NGX_ERROR;
        }

        return NGX_OK;
    }

    ngx_http_wasm_runtime_cleanup_exec_ctx(&exec);

    return NGX_OK;
}

static ngx_buf_t *ngx_http_wasm_body_filter_create_buf(ngx_http_request_t *r,
                                                       ngx_str_t *chunk,
                                                       ngx_buf_t *src) {
    ngx_buf_t *dst;

    dst = ngx_calloc_buf(r->pool);
    if (dst == NULL) {
        return NULL;
    }

    dst->pos = chunk->data;
    dst->last = chunk->data + chunk->len;
    dst->memory = 1;
    dst->flush = src->flush;
    dst->sync = src->sync;
    dst->last_buf = src->last_buf;
    dst->last_in_chain = src->last_in_chain;

    return dst;
}

static ngx_int_t ngx_http_wasm_body_filter_append(ngx_http_request_t *r,
                                                  ngx_chain_t **head,
                                                  ngx_chain_t **tail,
                                                  ngx_buf_t *buf) {
    ngx_chain_t *cl;

    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = buf;
    cl->next = NULL;

    if (*tail == NULL) {
        *head = cl;
    } else {
        (*tail)->next = cl;
    }

    *tail = cl;

    return NGX_OK;
}
