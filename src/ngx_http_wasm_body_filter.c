#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <ngx_http_wasm_module_int.h>

#define NGX_HTTP_WASM_BUFFERED 0x08

static ngx_http_output_body_filter_pt ngx_http_wasm_next_body_filter;

static ngx_http_wasm_ctx_t *
ngx_http_wasm_body_filter_get_or_create_ctx(ngx_http_request_t *r);
static ngx_http_wasm_exec_ctx_t *
ngx_http_wasm_body_filter_get_or_init_exec(ngx_http_request_t *r,
                                           ngx_http_wasm_conf_t *wcf,
                                           ngx_http_wasm_main_conf_t *wmcf,
                                           ngx_http_wasm_ctx_t *ctx);
static ngx_int_t
ngx_http_wasm_body_filter_process(ngx_http_request_t *r,
                                  ngx_http_wasm_conf_t *wcf,
                                  ngx_http_wasm_main_conf_t *wmcf,
                                  ngx_http_wasm_ctx_t *ctx,
                                  ngx_chain_t *in,
                                  ngx_chain_t *out,
                                  ngx_chain_t **tail);
static ngx_int_t
ngx_http_wasm_body_filter_run_chunk(ngx_http_request_t *r,
                                    ngx_http_wasm_ctx_t *ctx,
                                    ngx_http_wasm_conf_t *wcf,
                                    ngx_http_wasm_main_conf_t *wmcf,
                                    ngx_buf_t *buf,
                                    const u_char *data,
                                    size_t len,
                                    ngx_uint_t eof,
                                    ngx_buf_t **out);
static ngx_int_t ngx_http_wasm_body_filter_queue_tail(ngx_http_request_t *r,
                                                      ngx_http_wasm_ctx_t *ctx,
                                                      ngx_chain_t *in);
#if (NGX_THREADS)
static ngx_int_t
ngx_http_wasm_body_filter_schedule_file_chunk(ngx_http_request_t *r,
                                              ngx_http_wasm_conf_t *wcf,
                                              ngx_http_wasm_ctx_t *ctx,
                                              ngx_chain_t *cl);
static ngx_int_t
ngx_http_wasm_body_filter_complete_file_chunk(ngx_http_request_t *r,
                                              ngx_http_wasm_conf_t *wcf,
                                              ngx_http_wasm_main_conf_t *wmcf,
                                              ngx_http_wasm_ctx_t *ctx,
                                              ngx_chain_t **out,
                                              ngx_chain_t **tail);
static ngx_int_t
ngx_http_wasm_body_filter_queue_remaining(ngx_http_request_t *r,
                                          ngx_http_wasm_ctx_t *ctx,
                                          ngx_chain_t *cl,
                                          off_t file_pos);
#endif
static ngx_int_t ngx_http_wasm_body_filter_append(ngx_http_request_t *r,
                                                  ngx_chain_t **head,
                                                  ngx_chain_t **tail,
                                                  ngx_buf_t *buf);
static ngx_int_t ngx_http_wasm_body_filter_enqueue(ngx_http_request_t *r,
                                                   ngx_http_wasm_ctx_t *ctx,
                                                   ngx_buf_t *buf);
static ngx_buf_t *ngx_http_wasm_body_filter_create_buf(ngx_http_request_t *r,
                                                       ngx_str_t *chunk,
                                                       ngx_buf_t *src);
static ngx_buf_t *ngx_http_wasm_body_filter_copy_buf(ngx_http_request_t *r,
                                                     ngx_buf_t *src);

#if (NGX_THREADS)
static ngx_int_t
ngx_http_wasm_body_filter_thread_handler(ngx_thread_task_t *task,
                                         ngx_file_t *file);
static void ngx_http_wasm_body_filter_thread_event_handler(ngx_event_t *ev);
#endif

ngx_int_t ngx_http_wasm_body_filter_init_process(void) {
    ngx_http_wasm_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_wasm_body_filter_handler;

    return NGX_OK;
}

ngx_int_t ngx_http_wasm_body_filter_handler(ngx_http_request_t *r,
                                            ngx_chain_t *in) {
    static ngx_str_t phase_name = ngx_string("body filter");
    ngx_http_wasm_conf_t *wcf;
    ngx_http_wasm_ctx_t *ctx;
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_chain_t *out;
    ngx_chain_t *tail;
    ngx_int_t rc;

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

    ctx = ngx_http_wasm_body_filter_get_or_create_ctx(r);
    if (ctx == NULL) {
        return NGX_ERROR;
    }

    if (ctx->body_filter_waiting) {
        if (ngx_http_wasm_body_filter_queue_tail(r, ctx, in) != NGX_OK) {
            return NGX_ERROR;
        }

        r->buffered |= NGX_HTTP_WASM_BUFFERED;
        return NGX_AGAIN;
    }

    out = NULL;
    tail = NULL;

    rc = ngx_http_wasm_body_filter_process(r, wcf, wmcf, ctx, in, out, &tail);
    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    return rc;
}

static ngx_http_wasm_ctx_t *
ngx_http_wasm_body_filter_get_or_create_ctx(ngx_http_request_t *r) {
    ngx_pool_cleanup_t *cln;
    ngx_http_wasm_ctx_t *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_wasm_module);
    if (ctx != NULL) {
        return ctx;
    }

    ctx = ngx_pcalloc(r->pool, sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }

    ngx_http_set_ctx(r, ctx, ngx_http_wasm_module);

    cln = ngx_pool_cleanup_add(r->pool, 0);
    if (cln == NULL) {
        return NULL;
    }

    cln->handler = ngx_http_wasm_cleanup_ctx;
    cln->data = ctx;

    ctx->body_filter_pending_last = &ctx->body_filter_pending;

    return ctx;
}

static ngx_http_wasm_exec_ctx_t *
ngx_http_wasm_body_filter_get_or_init_exec(ngx_http_request_t *r,
                                           ngx_http_wasm_conf_t *wcf,
                                           ngx_http_wasm_main_conf_t *wmcf,
                                           ngx_http_wasm_ctx_t *ctx) {
    if (!ctx->body_filter_exec_set) {
        ngx_http_wasm_runtime_init_exec_ctx(&ctx->body_filter_exec,
                                            r,
                                            &wcf->body_filter,
                                            NGX_HTTP_WASM_PHASE_BODY_FILTER,
                                            wmcf->runtime);
        ctx->body_filter_exec.fuel_limit = wcf->fuel_limit;
        ctx->body_filter_exec.timeslice_fuel = wcf->timeslice_fuel;
        ctx->body_filter_exec.fuel_remaining = wcf->fuel_limit;
        ctx->body_filter_exec_set = 1;
    }

    return &ctx->body_filter_exec;
}

static ngx_int_t
ngx_http_wasm_body_filter_process(ngx_http_request_t *r,
                                  ngx_http_wasm_conf_t *wcf,
                                  ngx_http_wasm_main_conf_t *wmcf,
                                  ngx_http_wasm_ctx_t *ctx,
                                  ngx_chain_t *in,
                                  ngx_chain_t *out,
                                  ngx_chain_t **tail) {
    ngx_chain_t *cl;
    ngx_buf_t *buf;
    ngx_buf_t *replacement;
    size_t len;
    ngx_uint_t eof;
    ngx_int_t rc;

    for (cl = in; cl != NULL; cl = cl->next) {
        buf = cl->buf;
        eof = buf->last_buf || buf->last_in_chain;

        if (ngx_buf_in_memory(buf)) {
            len = (size_t)(buf->last - buf->pos);

            if (len != 0 || eof) {
                rc = ngx_http_wasm_body_filter_run_chunk(
                    r, ctx, wcf, wmcf, buf, buf->pos, len, eof, &replacement);
                if (rc != NGX_OK) {
                    return NGX_ERROR;
                }

                if (replacement != NULL) {
                    if (ngx_http_wasm_body_filter_append(
                            r, &out, tail, replacement) != NGX_OK) {
                        return NGX_ERROR;
                    }

                    continue;
                }
            }

            if (ngx_http_wasm_body_filter_append(r, &out, tail, buf) !=
                NGX_OK) {
                return NGX_ERROR;
            }

            continue;
        }

        if (buf->in_file && wcf->body_filter_file_chunk_size != 0) {
#if (NGX_THREADS)
            if (out != NULL) {
                rc = ngx_http_wasm_next_body_filter(r, out);
                if (rc == NGX_ERROR) {
                    return NGX_ERROR;
                }
            }

            if (ngx_http_wasm_body_filter_schedule_file_chunk(
                    r, wcf, ctx, cl) != NGX_OK) {
                return NGX_ERROR;
            }

            r->buffered |= NGX_HTTP_WASM_BUFFERED;
            return NGX_AGAIN;
#else
            if (ngx_http_wasm_body_filter_append(r, &out, tail, buf) !=
                NGX_OK) {
                return NGX_ERROR;
            }

            continue;
#endif
        }

        if (ngx_buf_special(buf) && eof) {
            rc = ngx_http_wasm_body_filter_run_chunk(
                r, ctx, wcf, wmcf, buf, (u_char *)"", 0, eof, &replacement);
            if (rc != NGX_OK) {
                return NGX_ERROR;
            }

            if (replacement != NULL) {
                if (ngx_http_wasm_body_filter_append(
                        r, &out, tail, replacement) != NGX_OK) {
                    return NGX_ERROR;
                }

                continue;
            }
        }

        if (ngx_http_wasm_body_filter_append(r, &out, tail, buf) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    if (out == NULL) {
        if (!ctx->body_filter_waiting && ctx->body_filter_pending == NULL) {
            r->buffered &= ~NGX_HTTP_WASM_BUFFERED;
        }
        return NGX_OK;
    }

    rc = ngx_http_wasm_next_body_filter(r, out);
    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    if (ctx->body_filter_waiting || ctx->body_filter_pending != NULL) {
        r->buffered |= NGX_HTTP_WASM_BUFFERED;
    } else {
        r->buffered &= ~NGX_HTTP_WASM_BUFFERED;
    }

    return rc;
}

static ngx_int_t
ngx_http_wasm_body_filter_run_chunk(ngx_http_request_t *r,
                                    ngx_http_wasm_ctx_t *ctx,
                                    ngx_http_wasm_conf_t *wcf,
                                    ngx_http_wasm_main_conf_t *wmcf,
                                    ngx_buf_t *buf,
                                    const u_char *data,
                                    size_t len,
                                    ngx_uint_t eof,
                                    ngx_buf_t **out) {
    ngx_http_wasm_exec_ctx_t *exec;
    ngx_int_t rc;

    *out = NULL;

    exec = ngx_http_wasm_body_filter_get_or_init_exec(r, wcf, wmcf, ctx);
    if (exec == NULL) {
        return NGX_ERROR;
    }

    rc =
        ngx_http_wasm_abi_resp_set_body_chunk_input(&exec->abi, data, len, eof);
    if (rc != NGX_HTTP_WASM_OK) {
        return NGX_ERROR;
    }

    rc = ngx_http_wasm_runtime_run(exec);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    if (exec->abi.resp_body_chunk_output_set) {
        *out = ngx_http_wasm_body_filter_create_buf(
            r, &exec->abi.resp_body_chunk_output, buf);

        if (*out == NULL) {
            return NGX_ERROR;
        }

        return NGX_OK;
    }

    return NGX_OK;
}

#if (NGX_THREADS)

static ngx_int_t
ngx_http_wasm_body_filter_schedule_file_chunk(ngx_http_request_t *r,
                                              ngx_http_wasm_conf_t *wcf,
                                              ngx_http_wasm_ctx_t *ctx,
                                              ngx_chain_t *cl) {
    ngx_buf_t *buf;
    off_t available;
    size_t size;

    buf = cl->buf;
    available = buf->file_last - buf->file_pos;
    if (available <= 0) {
        return ngx_http_wasm_body_filter_queue_remaining(r, ctx, cl->next, 0);
    }

    size = (size_t)available;
    if (size > wcf->body_filter_file_chunk_size) {
        size = wcf->body_filter_file_chunk_size;
    }

    ctx->body_filter_file = *buf->file;
    ctx->body_filter_file.log = r->connection->log;
    ctx->body_filter_file.thread_task = ctx->body_filter_thread_task;
    ctx->body_filter_file.thread_handler =
        ngx_http_wasm_body_filter_thread_handler;
    ctx->body_filter_file.thread_ctx = r;
    ctx->body_filter_read_offset = buf->file_pos;
    ctx->body_filter_read_size = size;
    ctx->body_filter_read_buf = ngx_pnalloc(r->pool, size);
    if (ctx->body_filter_read_buf == NULL) {
        return NGX_ERROR;
    }

    ctx->body_filter_chunk_flush = 0;
    ctx->body_filter_chunk_sync = 0;
    ctx->body_filter_chunk_last_buf = 0;
    ctx->body_filter_chunk_last_in_chain = 0;

    if ((off_t)size == available) {
        ctx->body_filter_chunk_flush = buf->flush;
        ctx->body_filter_chunk_sync = buf->sync;
        ctx->body_filter_chunk_last_buf = buf->last_buf;
        ctx->body_filter_chunk_last_in_chain = buf->last_in_chain;
    }

    if (ngx_http_wasm_body_filter_queue_remaining(
            r, ctx, cl, buf->file_pos + (off_t)size) != NGX_OK) {
        return NGX_ERROR;
    }

    ctx->body_filter_file.thread_task = ctx->body_filter_thread_task;
    if (ngx_thread_read(&ctx->body_filter_file,
                        ctx->body_filter_read_buf,
                        ctx->body_filter_read_size,
                        ctx->body_filter_read_offset,
                        r->pool) != NGX_AGAIN) {
        ctx->body_filter_thread_task = ctx->body_filter_file.thread_task;
        return NGX_ERROR;
    }

    ctx->body_filter_thread_task = ctx->body_filter_file.thread_task;
    ctx->body_filter_waiting = 1;

    return NGX_OK;
}

static ngx_int_t
ngx_http_wasm_body_filter_complete_file_chunk(ngx_http_request_t *r,
                                              ngx_http_wasm_conf_t *wcf,
                                              ngx_http_wasm_main_conf_t *wmcf,
                                              ngx_http_wasm_ctx_t *ctx,
                                              ngx_chain_t **out,
                                              ngx_chain_t **tail) {
    ngx_buf_t src;
    ngx_buf_t *replacement;
    ssize_t n;

    ctx->body_filter_file.thread_task = ctx->body_filter_thread_task;
    n = ngx_thread_read(&ctx->body_filter_file,
                        ctx->body_filter_read_buf,
                        ctx->body_filter_read_size,
                        ctx->body_filter_read_offset,
                        r->pool);
    ctx->body_filter_thread_task = ctx->body_filter_file.thread_task;

    if (n == NGX_ERROR || (size_t)n != ctx->body_filter_read_size) {
        return NGX_ERROR;
    }

    ngx_memzero(&src, sizeof(src));
    src.flush = ctx->body_filter_chunk_flush;
    src.sync = ctx->body_filter_chunk_sync;
    src.last_buf = ctx->body_filter_chunk_last_buf;
    src.last_in_chain = ctx->body_filter_chunk_last_in_chain;

    if (ngx_http_wasm_body_filter_run_chunk(r,
                                            ctx,
                                            wcf,
                                            wmcf,
                                            &src,
                                            ctx->body_filter_read_buf,
                                            ctx->body_filter_read_size,
                                            src.last_buf || src.last_in_chain,
                                            &replacement) != NGX_OK) {
        return NGX_ERROR;
    }

    if (replacement != NULL) {
        return ngx_http_wasm_body_filter_append(r, out, tail, replacement);
    }

    replacement = ngx_http_wasm_body_filter_create_buf(
        r,
        &(ngx_str_t){ctx->body_filter_read_size, ctx->body_filter_read_buf},
        &src);
    if (replacement == NULL) {
        return NGX_ERROR;
    }

    return ngx_http_wasm_body_filter_append(r, out, tail, replacement);
}

static ngx_int_t
ngx_http_wasm_body_filter_thread_handler(ngx_thread_task_t *task,
                                         ngx_file_t *file) {
    ngx_str_t name;
    ngx_thread_pool_t *tp;
    ngx_http_request_t *r;
    ngx_http_core_loc_conf_t *clcf;

    r = file->thread_ctx;
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    tp = clcf->thread_pool;

    if (tp == NULL) {
        if (ngx_http_complex_value(r, clcf->thread_pool_value, &name) !=
            NGX_OK) {
            return NGX_ERROR;
        }

        tp = ngx_thread_pool_get((ngx_cycle_t *)ngx_cycle, &name);
        if (tp == NULL) {
            ngx_log_error(NGX_LOG_ERR,
                          r->connection->log,
                          0,
                          "thread pool \"%V\" not found",
                          &name);
            return NGX_ERROR;
        }
    }

    task->event.data = r;
    task->event.handler = ngx_http_wasm_body_filter_thread_event_handler;

    if (ngx_thread_task_post(tp, task) != NGX_OK) {
        return NGX_ERROR;
    }

    ngx_add_timer(&task->event, 60000);

    r->main->blocked++;
    r->aio = 1;

    return NGX_OK;
}

static void ngx_http_wasm_body_filter_thread_event_handler(ngx_event_t *ev) {
    ngx_connection_t *c;
    ngx_http_request_t *r;
    ngx_http_wasm_conf_t *wcf;
    ngx_http_wasm_ctx_t *ctx;
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_chain_t *in;
    ngx_chain_t *out;
    ngx_chain_t *tail;
    ngx_int_t rc;

    r = ev->data;
    c = r->connection;

    ngx_http_set_log_request(c->log, r);

    if (ev->timedout) {
        ngx_log_error(
            NGX_LOG_ALERT, c->log, 0, "ngx_wasm body filter thread timed out");
        ev->timedout = 0;
        return;
    }

    if (ev->timer_set) {
        ngx_del_timer(ev);
    }

    r->main->blocked--;
    r->aio = 0;

    if (r->done || r->main->terminated) {
        c->write->handler(c->write);
        return;
    }

    wcf = ngx_http_get_module_loc_conf(r, ngx_http_wasm_module);
    wmcf = ngx_http_get_module_main_conf(r, ngx_http_wasm_module);
    ctx = ngx_http_get_module_ctx(r, ngx_http_wasm_module);

    if (wcf == NULL || wmcf == NULL || ctx == NULL) {
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    out = NULL;
    tail = NULL;

    rc = ngx_http_wasm_body_filter_complete_file_chunk(
        r, wcf, wmcf, ctx, &out, &tail);
    if (rc != NGX_OK) {
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    ctx->body_filter_waiting = 0;
    in = ctx->body_filter_pending;
    ctx->body_filter_pending = NULL;
    ctx->body_filter_pending_last = &ctx->body_filter_pending;

    rc = ngx_http_wasm_body_filter_process(r, wcf, wmcf, ctx, in, out, &tail);
    if (rc == NGX_ERROR) {
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    if (ctx->body_filter_waiting || ctx->body_filter_pending != NULL) {
        r->buffered |= NGX_HTTP_WASM_BUFFERED;
    } else {
        r->buffered &= ~NGX_HTTP_WASM_BUFFERED;
    }

    r->write_event_handler(r);
    ngx_http_run_posted_requests(c);
}

#endif

static ngx_int_t ngx_http_wasm_body_filter_queue_tail(ngx_http_request_t *r,
                                                      ngx_http_wasm_ctx_t *ctx,
                                                      ngx_chain_t *in) {
    ngx_chain_t *cl;
    ngx_buf_t *buf;

    for (cl = in; cl != NULL; cl = cl->next) {
        buf = ngx_http_wasm_body_filter_copy_buf(r, cl->buf);
        if (buf == NULL) {
            return NGX_ERROR;
        }

        if (ngx_http_wasm_body_filter_enqueue(r, ctx, buf) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

#if (NGX_THREADS)
static ngx_int_t
ngx_http_wasm_body_filter_queue_remaining(ngx_http_request_t *r,
                                          ngx_http_wasm_ctx_t *ctx,
                                          ngx_chain_t *cl,
                                          off_t file_pos) {
    ngx_buf_t *buf;

    if (cl == NULL) {
        return NGX_OK;
    }

    buf = cl->buf;

    if (file_pos != 0 && buf->in_file) {
        buf = ngx_http_wasm_body_filter_copy_buf(r, buf);
        if (buf == NULL) {
            return NGX_ERROR;
        }

        buf->file_pos = file_pos;
        if (buf->file_last < buf->file_pos) {
            buf->file_last = buf->file_pos;
        }

        if (ngx_http_wasm_body_filter_enqueue(r, ctx, buf) != NGX_OK) {
            return NGX_ERROR;
        }

        return ngx_http_wasm_body_filter_queue_tail(r, ctx, cl->next);
    }

    return ngx_http_wasm_body_filter_queue_tail(r, ctx, cl);
}
#endif

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

static ngx_int_t ngx_http_wasm_body_filter_enqueue(ngx_http_request_t *r,
                                                   ngx_http_wasm_ctx_t *ctx,
                                                   ngx_buf_t *buf) {
    ngx_chain_t *cl;

    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = buf;
    cl->next = NULL;

    *ctx->body_filter_pending_last = cl;
    ctx->body_filter_pending_last = &cl->next;

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

static ngx_buf_t *ngx_http_wasm_body_filter_copy_buf(ngx_http_request_t *r,
                                                     ngx_buf_t *src) {
    ngx_buf_t *dst;
    size_t len;
    u_char *p;

    dst = ngx_calloc_buf(r->pool);
    if (dst == NULL) {
        return NULL;
    }

    *dst = *src;

    if (ngx_buf_in_memory(src)) {
        len = (size_t)(src->last - src->pos);
        if (len != 0) {
            p = ngx_pnalloc(r->pool, len);
            if (p == NULL) {
                return NULL;
            }

            ngx_memcpy(p, src->pos, len);
            dst->start = p;
            dst->pos = p;
            dst->last = p + len;
            dst->end = p + len;
        }

        dst->memory = 1;
        dst->temporary = 0;
        dst->mmap = 0;
        dst->in_file = 0;
        dst->shadow = NULL;
    }

    return dst;
}
