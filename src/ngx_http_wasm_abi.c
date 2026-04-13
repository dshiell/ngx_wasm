#include <ngx_http_wasm_abi.h>

static ngx_table_elt_t *ngx_http_wasm_abi_find_header(ngx_http_request_t *r,
                                                      const u_char *name,
                                                      size_t name_len);
static ngx_table_elt_t *ngx_http_wasm_abi_find_resp_header(
    ngx_http_request_t *r, const u_char *name, size_t name_len);
static ngx_int_t ngx_http_wasm_abi_copy_bytes(ngx_http_request_t *r,
                                              ngx_str_t *dst,
                                              const u_char *data,
                                              size_t len);
static ngx_int_t ngx_http_wasm_abi_require(ngx_http_wasm_abi_ctx_t *ctx,
                                           ngx_uint_t capability);
static ngx_uint_t ngx_http_wasm_abi_header_eq(const u_char *name,
                                              size_t name_len,
                                              const char *literal);
static ngx_table_elt_t **ngx_http_wasm_abi_resp_header_slot(
    ngx_http_headers_out_t *out, const u_char *name, size_t name_len);
static ngx_int_t
ngx_http_wasm_abi_resp_set_cached_header(ngx_http_wasm_abi_ctx_t *ctx,
                                         const u_char *name,
                                         size_t name_len,
                                         const u_char *value,
                                         size_t value_len);
static ngx_int_t
ngx_http_wasm_abi_resp_get_cached_header(ngx_http_wasm_abi_ctx_t *ctx,
                                         const u_char *name,
                                         size_t name_len,
                                         u_char *buf,
                                         size_t buf_len);

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
                            ngx_http_request_t *r,
                            ngx_uint_t capabilities) {
    ngx_memzero(ctx, sizeof(*ctx));

    ctx->request = r;
    ctx->abi_version = NGX_HTTP_WASM_ABI_VERSION;
    ctx->capabilities = capabilities;
    ctx->status = NGX_HTTP_OK;
}

static ngx_int_t ngx_http_wasm_abi_require(ngx_http_wasm_abi_ctx_t *ctx,
                                           ngx_uint_t capability) {
    if ((ctx->capabilities & capability) == 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    return NGX_HTTP_WASM_OK;
}

static ngx_uint_t ngx_http_wasm_abi_header_eq(const u_char *name,
                                              size_t name_len,
                                              const char *literal) {
    size_t literal_len;

    literal_len = ngx_strlen(literal);

    return name_len == literal_len &&
           ngx_strncasecmp((u_char *)name, (u_char *)literal, literal_len) == 0;
}

static ngx_table_elt_t **ngx_http_wasm_abi_resp_header_slot(
    ngx_http_headers_out_t *out, const u_char *name, size_t name_len) {
    switch (name_len) {
        case 4:
            if (ngx_http_wasm_abi_header_eq(name, name_len, "date")) {
                return &out->date;
            }

            if (ngx_http_wasm_abi_header_eq(name, name_len, "etag")) {
                return &out->etag;
            }

            if (ngx_http_wasm_abi_header_eq(name, name_len, "link")) {
                return &out->link;
            }

            break;

        case 6:
            if (ngx_http_wasm_abi_header_eq(name, name_len, "server")) {
                return &out->server;
            }

            break;

        case 7:
            if (ngx_http_wasm_abi_header_eq(name, name_len, "expires")) {
                return &out->expires;
            }

            if (ngx_http_wasm_abi_header_eq(name, name_len, "refresh")) {
                return &out->refresh;
            }

            break;

        case 8:
            if (ngx_http_wasm_abi_header_eq(name, name_len, "location")) {
                return &out->location;
            }

            break;

        case 13:
            if (ngx_http_wasm_abi_header_eq(name, name_len, "cache-control")) {
                return &out->cache_control;
            }

            break;

        case 14:
            if (ngx_http_wasm_abi_header_eq(name, name_len, "content-length")) {
                return &out->content_length;
            }

            if (ngx_http_wasm_abi_header_eq(name, name_len, "content-range")) {
                return &out->content_range;
            }

            if (ngx_http_wasm_abi_header_eq(name, name_len, "last-modified")) {
                return &out->last_modified;
            }

            break;

        case 15:
            if (ngx_http_wasm_abi_header_eq(name, name_len, "accept-ranges")) {
                return &out->accept_ranges;
            }

            break;

        case 16:
            if (ngx_http_wasm_abi_header_eq(
                    name, name_len, "content-encoding")) {
                return &out->content_encoding;
            }

            break;

        case 17:
            if (ngx_http_wasm_abi_header_eq(
                    name, name_len, "www-authenticate")) {
                return &out->www_authenticate;
            }

            break;

        case 18:
            if (ngx_http_wasm_abi_header_eq(
                    name, name_len, "proxy-authenticate")) {
                return &out->proxy_authenticate;
            }

            break;
    }

    return NULL;
}

static ngx_int_t
ngx_http_wasm_abi_resp_set_cached_header(ngx_http_wasm_abi_ctx_t *ctx,
                                         const u_char *name,
                                         size_t name_len,
                                         const u_char *value,
                                         size_t value_len) {
    ngx_http_request_t *r;
    ngx_http_headers_out_t *out;
    ngx_table_elt_t **slot;
    ngx_table_elt_t *h;
    off_t n;
    time_t t;

    r = ctx->request;
    out = &r->headers_out;

    if (ngx_http_wasm_abi_header_eq(name, name_len, "content-type")) {
        if (ngx_http_wasm_abi_set_str(
                ctx, &out->content_type, value, value_len, 1) != NGX_OK) {
            return NGX_HTTP_WASM_ERROR;
        }

        out->content_type_len = value_len;
        out->content_type_lowcase = NULL;
        out->content_type_hash = 0;
        return NGX_HTTP_WASM_OK;
    }

    slot = ngx_http_wasm_abi_resp_header_slot(out, name, name_len);
    if (slot == NULL) {
        return NGX_HTTP_WASM_ERROR;
    }

    h = *slot;
    if (h == NULL) {
        h = ngx_list_push(&out->headers);
        if (h == NULL) {
            return NGX_HTTP_WASM_ERROR;
        }

        ngx_memzero(h, sizeof(*h));

        if (ngx_http_wasm_abi_copy_bytes(r, &h->key, name, name_len) !=
            NGX_HTTP_WASM_OK) {
            return NGX_HTTP_WASM_ERROR;
        }

        *slot = h;
    }

    h->hash = 1;

    if (ngx_http_wasm_abi_copy_bytes(r, &h->value, value, value_len) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (slot == &out->content_length) {
        n = (value_len != 0) ? ngx_atoof((u_char *)value, value_len) : -1;
        out->content_length_n = n;
    }

    if (slot == &out->date) {
        t = (value_len != 0) ? ngx_parse_http_time((u_char *)value, value_len)
                             : -1;
        out->date_time = t;
    }

    if (slot == &out->last_modified) {
        t = (value_len != 0) ? ngx_parse_http_time((u_char *)value, value_len)
                             : -1;
        out->last_modified_time = t;
    }

    return NGX_HTTP_WASM_OK;
}

static ngx_int_t
ngx_http_wasm_abi_resp_get_cached_header(ngx_http_wasm_abi_ctx_t *ctx,
                                         const u_char *name,
                                         size_t name_len,
                                         u_char *buf,
                                         size_t buf_len) {
    ngx_http_headers_out_t *out;
    ngx_table_elt_t **slot;
    ngx_str_t value;
    size_t copy_len;

    out = &ctx->request->headers_out;

    if (ngx_http_wasm_abi_header_eq(name, name_len, "content-type")) {
        if (out->content_type.len == 0) {
            return NGX_HTTP_WASM_NOT_FOUND;
        }

        value = out->content_type;

    } else {
        slot = ngx_http_wasm_abi_resp_header_slot(out, name, name_len);
        if (slot == NULL || *slot == NULL) {
            return NGX_HTTP_WASM_NOT_FOUND;
        }

        value = (*slot)->value;
    }

    copy_len = ngx_min(value.len, buf_len);
    if (copy_len != 0) {
        ngx_memcpy(buf, value.data, copy_len);
    }

    return (ngx_int_t)value.len;
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

static ngx_int_t ngx_http_wasm_abi_copy_bytes(ngx_http_request_t *r,
                                              ngx_str_t *dst,
                                              const u_char *data,
                                              size_t len) {
    u_char *p;

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        return NGX_HTTP_WASM_ERROR;
    }

    ngx_memcpy(p, data, len);
    dst->data = p;
    dst->len = len;

    return NGX_HTTP_WASM_OK;
}

static ngx_table_elt_t *ngx_http_wasm_abi_find_header(ngx_http_request_t *r,
                                                      const u_char *name,
                                                      size_t name_len) {
    ngx_list_part_t *part;
    ngx_table_elt_t *h;
    ngx_uint_t i;

    part = &r->headers_in.headers.part;
    h = part->elts;

    for (i = 0;; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].key.len == name_len &&
            ngx_strncasecmp(h[i].key.data, (u_char *)name, name_len) == 0) {
            return &h[i];
        }
    }

    return NULL;
}

static ngx_table_elt_t *ngx_http_wasm_abi_find_resp_header(
    ngx_http_request_t *r, const u_char *name, size_t name_len) {
    ngx_list_part_t *part;
    ngx_table_elt_t *h;
    ngx_uint_t i;

    part = &r->headers_out.headers.part;
    h = part->elts;

    for (i = 0;; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].hash != 0 && h[i].key.len == name_len &&
            ngx_strncasecmp(h[i].key.data, (u_char *)name, name_len) == 0) {
            return &h[i];
        }
    }

    return NULL;
}

ngx_int_t ngx_http_wasm_abi_resp_set_status(ngx_http_wasm_abi_ctx_t *ctx,
                                            ngx_int_t status) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_RESP_STATUS_SET) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    ctx->status = status;
    ctx->status_set = 1;

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_abi_req_set_header(ngx_http_wasm_abi_ctx_t *ctx,
                                           const u_char *name,
                                           size_t name_len,
                                           const u_char *value,
                                           size_t value_len) {
    ngx_http_request_t *r;
    ngx_table_elt_t *h;
    u_char *lowcase_key;
    ngx_uint_t hash;

    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_REQ_HEADERS_RW) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (name_len == 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    r = ctx->request;
    h = ngx_http_wasm_abi_find_header(r, name, name_len);

    if (h == NULL) {
        h = ngx_list_push(&r->headers_in.headers);
        if (h == NULL) {
            return NGX_HTTP_WASM_ERROR;
        }

        ngx_memzero(h, sizeof(*h));

        if (ngx_http_wasm_abi_copy_bytes(r, &h->key, name, name_len) !=
            NGX_HTTP_WASM_OK) {
            return NGX_HTTP_WASM_ERROR;
        }

        lowcase_key = ngx_pnalloc(r->pool, name_len);
        if (lowcase_key == NULL) {
            return NGX_HTTP_WASM_ERROR;
        }

        hash = ngx_hash_strlow(lowcase_key, h->key.data, name_len);
        h->hash = hash;
        h->lowcase_key = lowcase_key;
    }

    if (ngx_http_wasm_abi_copy_bytes(r, &h->value, value, value_len) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_abi_req_get_header(ngx_http_wasm_abi_ctx_t *ctx,
                                           const u_char *name,
                                           size_t name_len,
                                           u_char *buf,
                                           size_t buf_len) {
    ngx_http_request_t *r;
    ngx_table_elt_t *h;
    size_t copy_len;

    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_REQ_HEADERS_RO) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (name_len == 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    r = ctx->request;
    h = ngx_http_wasm_abi_find_header(r, name, name_len);
    if (h == NULL) {
        return NGX_HTTP_WASM_NOT_FOUND;
    }

    copy_len = ngx_min(h->value.len, buf_len);
    if (copy_len != 0) {
        ngx_memcpy(buf, h->value.data, copy_len);
    }

    return (ngx_int_t)h->value.len;
}

ngx_int_t ngx_http_wasm_abi_req_set_body(ngx_http_wasm_abi_ctx_t *ctx,
                                         const u_char *data,
                                         size_t len) {
    if (ngx_http_wasm_abi_set_str(ctx, &ctx->request_body, data, len, 1) !=
        NGX_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    ctx->request_body_set = 1;

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_abi_req_get_body(ngx_http_wasm_abi_ctx_t *ctx,
                                         u_char *buf,
                                         size_t buf_len) {
    size_t copy_len;

    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_REQ_BODY_GET) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (!ctx->request_body_set) {
        return NGX_HTTP_WASM_NOT_FOUND;
    }

    copy_len = ngx_min(ctx->request_body.len, buf_len);
    if (copy_len != 0) {
        ngx_memcpy(buf, ctx->request_body.data, copy_len);
    }

    return (ngx_int_t)ctx->request_body.len;
}

ngx_int_t ngx_http_wasm_abi_resp_set_header(ngx_http_wasm_abi_ctx_t *ctx,
                                            const u_char *name,
                                            size_t name_len,
                                            const u_char *value,
                                            size_t value_len) {
    ngx_http_request_t *r;
    ngx_table_elt_t *h;

    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_RESP_HEADERS_RW) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (name_len == 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    r = ctx->request;
    if (ngx_http_wasm_abi_header_eq(name, name_len, "content-type") ||
        ngx_http_wasm_abi_resp_header_slot(&r->headers_out, name, name_len) !=
            NULL) {
        return ngx_http_wasm_abi_resp_set_cached_header(
            ctx, name, name_len, value, value_len);
    }

    h = ngx_http_wasm_abi_find_resp_header(r, name, name_len);
    if (h == NULL) {
        h = ngx_list_push(&r->headers_out.headers);
        if (h == NULL) {
            return NGX_HTTP_WASM_ERROR;
        }

        ngx_memzero(h, sizeof(*h));

        if (ngx_http_wasm_abi_copy_bytes(r, &h->key, name, name_len) !=
            NGX_HTTP_WASM_OK) {
            return NGX_HTTP_WASM_ERROR;
        }

        h->hash = 1;
    }

    if (ngx_http_wasm_abi_copy_bytes(r, &h->value, value, value_len) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_abi_resp_get_header(ngx_http_wasm_abi_ctx_t *ctx,
                                            const u_char *name,
                                            size_t name_len,
                                            u_char *buf,
                                            size_t buf_len) {
    ngx_http_request_t *r;
    ngx_table_elt_t *h;
    ngx_str_t value;
    size_t copy_len;

    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_RESP_HEADERS_RW) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (name_len == 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    r = ctx->request;
    if (ngx_http_wasm_abi_header_eq(name, name_len, "content-type") ||
        ngx_http_wasm_abi_resp_header_slot(&r->headers_out, name, name_len) !=
            NULL) {
        return ngx_http_wasm_abi_resp_get_cached_header(
            ctx, name, name_len, buf, buf_len);
    }

    h = ngx_http_wasm_abi_find_resp_header(r, name, name_len);
    if (h == NULL) {
        return NGX_HTTP_WASM_NOT_FOUND;
    }

    value = h->value;

    copy_len = ngx_min(value.len, buf_len);
    if (copy_len != 0) {
        ngx_memcpy(buf, value.data, copy_len);
    }

    return (ngx_int_t)value.len;
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
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_RESP_BODY_WRITE) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

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

    if (!ctx->body_set || ctx->body.len == 0) {
        ctx->response_sent = 1;
        return ngx_http_send_special(ctx->request, NGX_HTTP_LAST);
    }

    b = ngx_calloc_buf(ctx->request->pool);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->pos = ctx->body.data;
    b->last = ctx->body.data + ctx->body.len;
    b->memory = 1;

    b->last_buf = (ctx->request == ctx->request->main) ? 1 : 0;
    b->last_in_chain = 1;

    out.buf = b;
    out.next = NULL;

    ctx->response_sent = 1;

    return ngx_http_output_filter(ctx->request, &out);
}
