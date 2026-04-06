#include <ngx_http_wasm_abi.h>

static ngx_int_t ngx_http_wasm_abi_set_str(ngx_http_wasm_abi_ctx_t *ctx,
                                           ngx_str_t *dst,
                                           const u_char *data,
                                           size_t len,
                                           ngx_uint_t copy) {
    u_char *p;

    if (copy) {
        p = ngx_pnalloc(ctx->request->pool, len);
        if (p == NULL) {
            return NGX_ERROR;
        }

        ngx_memcpy(p, data, len);
        dst->data = p;
        dst->len = len;

        return NGX_OK;
    }

    dst->data = (u_char *)data;
    dst->len = len;

    return NGX_OK;
}

void ngx_http_wasm_abi_init(ngx_http_wasm_abi_ctx_t *ctx,
                            ngx_http_request_t *r) {
    ngx_memzero(ctx, sizeof(*ctx));

    ctx->request = r;
    ctx->abi_version = NGX_HTTP_WASM_ABI_VERSION;
    ctx->status = NGX_HTTP_OK;
}

ngx_int_t ngx_http_wasm_abi_log(ngx_http_wasm_abi_ctx_t *ctx,
                                ngx_uint_t level,
                                const u_char *data,
                                size_t len) {
    ngx_log_error(level,
                  ctx->request->connection->log,
                  0,
                  "ngx_wasm guest: \"%*s\"",
                  (int)len,
                  data);

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_abi_resp_set_status(ngx_http_wasm_abi_ctx_t *ctx,
                                            ngx_int_t status) {
    ctx->status = status;
    ctx->status_set = 1;

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_abi_resp_set_content_type(ngx_http_wasm_abi_ctx_t *ctx,
                                                  const u_char *data,
                                                  size_t len,
                                                  ngx_uint_t copy) {
    if (ngx_http_wasm_abi_set_str(ctx, &ctx->content_type, data, len, copy) !=
        NGX_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    ctx->content_type_set = 1;

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_abi_resp_write(ngx_http_wasm_abi_ctx_t *ctx,
                                       const u_char *data,
                                       size_t len,
                                       ngx_uint_t copy) {
    if (ngx_http_wasm_abi_set_str(ctx, &ctx->body, data, len, copy) != NGX_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    ctx->body_set = 1;
    ctx->body_is_borrowed = !copy;

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_abi_send_response(ngx_http_wasm_abi_ctx_t *ctx) {
    ngx_buf_t *b;
    ngx_chain_t out;
    ngx_int_t rc;

    if (ctx->response_sent) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ctx->request->headers_out.status = ctx->status;

    if (ctx->body_set) {
        ctx->request->headers_out.content_length_n = (off_t)ctx->body.len;
    } else {
        ctx->request->headers_out.content_length_n = 0;
    }

    if (ctx->content_type_set) {
        ctx->request->headers_out.content_type = ctx->content_type;
    } else {
        ngx_str_set(&ctx->request->headers_out.content_type, "text/plain");
    }

    rc = ngx_http_send_header(ctx->request);
    if (rc == NGX_ERROR || rc > NGX_OK || ctx->request->header_only) {
        return rc;
    }

    b = ngx_calloc_buf(ctx->request->pool);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ctx->body_set) {
        b->pos = ctx->body.data;
        b->last = ctx->body.data + ctx->body.len;
        b->memory = 1;
    } else {
        b->pos = NULL;
        b->last = NULL;
        b->sync = 1;
    }

    b->last_buf = (ctx->request == ctx->request->main) ? 1 : 0;
    b->last_in_chain = 1;
    b->sync = (b->last_buf || b->memory) ? 0 : 1;

    out.buf = b;
    out.next = NULL;

    ctx->response_sent = 1;

    return ngx_http_output_filter(ctx->request, &out);
}
