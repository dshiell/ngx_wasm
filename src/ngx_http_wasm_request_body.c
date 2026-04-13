#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <ngx_http_wasm_module_int.h>

static ngx_int_t ngx_http_wasm_store_request_body(ngx_http_request_t *r,
                                                  ngx_http_wasm_ctx_t *ctx,
                                                  size_t limit);
static void ngx_http_wasm_request_body_handler(ngx_http_request_t *r);

ngx_int_t ngx_http_wasm_prepare_request_body(ngx_http_request_t *r,
                                             ngx_http_wasm_conf_t *wcf,
                                             ngx_http_wasm_ctx_t *ctx) {
    ngx_int_t rc;

    if (ctx->request_body_status != NGX_OK) {
        return ctx->request_body_status;
    }

    if (wcf->request_body_buffer_size == 0) {
        rc = ngx_http_discard_request_body(r);
        if (rc != NGX_OK) {
            return rc;
        }

        return NGX_OK;
    }

    if (ctx->request_body_ready) {
        return NGX_OK;
    }

    if (ctx->request_body_reading) {
        return NGX_DONE;
    }

    if (r->headers_in.content_length_n > 0 &&
        (uint64_t)r->headers_in.content_length_n >
            (uint64_t)wcf->request_body_buffer_size) {
        ngx_log_error(NGX_LOG_NOTICE,
                      r->connection->log,
                      0,
                      "ngx_wasm: rejecting request body content_length=%O "
                      "limit=%uz",
                      r->headers_in.content_length_n,
                      wcf->request_body_buffer_size);
        return NGX_HTTP_REQUEST_ENTITY_TOO_LARGE;
    }

    if (r->headers_in.content_length_n <= 0 && !r->headers_in.chunked) {
        if (ngx_http_wasm_abi_req_set_body(&ctx->exec.abi, (u_char *)"", 0) !=
            NGX_HTTP_WASM_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        ctx->request_body_ready = 1;

        return NGX_OK;
    }

    ctx->request_body_reading = 1;
    ctx->request_body_async = 0;
    ctx->request_body_status = NGX_OK;

    r->request_body_in_single_buf = 1;

    rc = ngx_http_read_client_request_body(r,
                                           ngx_http_wasm_request_body_handler);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        ctx->request_body_reading = 0;
        ctx->request_body_status = rc;
        return rc;
    }

    if (!ctx->request_body_reading) {
        /*
         * ngx_http_read_client_request_body() increments r->main->count
         * even when it completes synchronously and invokes the callback
         * inline. Balance that reference before continuing the phase.
         */
        r->main->count--;
    }

    if (ctx->request_body_status != NGX_OK) {
        return ctx->request_body_status;
    }

    if (ctx->request_body_ready) {
        return NGX_OK;
    }

    ctx->request_body_async = 1;

    return NGX_DONE;
}

static ngx_int_t ngx_http_wasm_store_request_body(ngx_http_request_t *r,
                                                  ngx_http_wasm_ctx_t *ctx,
                                                  size_t limit) {
    ngx_chain_t *cl;
    ngx_buf_t *buf;
    size_t len;
    u_char *p;
    u_char *data;

    if (r->request_body == NULL || r->request_body->bufs == NULL) {
        ngx_log_error(NGX_LOG_NOTICE,
                      r->connection->log,
                      0,
                      "ngx_wasm: request body buffers missing");
        return ngx_http_wasm_abi_req_set_body(
                   &ctx->exec.abi, (u_char *)"", 0) == NGX_HTTP_WASM_OK
                   ? NGX_OK
                   : NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (r->request_body->temp_file != NULL) {
        ngx_log_error(NGX_LOG_ERR,
                      r->connection->log,
                      0,
                      "ngx_wasm: temp-file-backed request body is not yet "
                      "supported");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    len = 0;

    for (cl = r->request_body->bufs; cl != NULL; cl = cl->next) {
        buf = cl->buf;
        len += (size_t)(buf->last - buf->pos);

        if (len > limit) {
            return NGX_HTTP_REQUEST_ENTITY_TOO_LARGE;
        }
    }

    if (len == 0) {
        ngx_log_error(NGX_LOG_NOTICE,
                      r->connection->log,
                      0,
                      "ngx_wasm: request body empty");
        return ngx_http_wasm_abi_req_set_body(
                   &ctx->exec.abi, (u_char *)"", 0) == NGX_HTTP_WASM_OK
                   ? NGX_OK
                   : NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    data = ngx_pnalloc(r->pool, len);
    if (data == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    p = data;
    for (cl = r->request_body->bufs; cl != NULL; cl = cl->next) {
        buf = cl->buf;
        p = ngx_cpymem(p, buf->pos, (size_t)(buf->last - buf->pos));
    }

    if (ngx_http_wasm_abi_req_set_body(&ctx->exec.abi, data, len) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ngx_log_error(NGX_LOG_NOTICE,
                  r->connection->log,
                  0,
                  "ngx_wasm: buffered request body len=%uz",
                  len);

    return NGX_OK;
}

static void ngx_http_wasm_request_body_handler(ngx_http_request_t *r) {
    ngx_http_wasm_conf_t *wcf;
    ngx_http_wasm_ctx_t *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_wasm_module);
    if (ctx == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    wcf = ngx_http_get_module_loc_conf(r, ngx_http_wasm_module);
    if (wcf == NULL || wcf->request_body_buffer_size == 0) {
        wcf = ngx_http_get_module_srv_conf(r, ngx_http_wasm_module);
    }

    if (wcf == NULL || wcf->request_body_buffer_size == 0) {
        ctx->request_body_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
    } else {
        ctx->request_body_status = ngx_http_wasm_store_request_body(
            r, ctx, wcf->request_body_buffer_size);
    }

    ctx->request_body_reading = 0;
    ctx->request_body_ready = (ctx->request_body_status == NGX_OK);
    r->preserve_body = 1;

    if (!ctx->request_body_async) {
        return;
    }

    if (ctx->request_body_status != NGX_OK) {
        ngx_http_finalize_request(r, ctx->request_body_status);
        return;
    }

    ngx_http_wasm_resume_handler(r);
}
