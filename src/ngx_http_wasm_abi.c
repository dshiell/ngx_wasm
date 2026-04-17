#include <ngx_http_wasm_abi.h>
#include <ngx_http_wasm_module_int.h>
#if (NGX_HTTP_SSL)
#include <openssl/pem.h>
#endif

static ngx_table_elt_t *ngx_http_wasm_abi_find_header(ngx_http_request_t *r,
                                                      const u_char *name,
                                                      size_t name_len);
static ngx_table_elt_t *ngx_http_wasm_abi_find_resp_header(
    ngx_http_request_t *r, const u_char *name, size_t name_len);
static ngx_http_variable_t *ngx_http_wasm_abi_find_variable(
    ngx_http_request_t *r, const u_char *name, size_t name_len);
static ngx_int_t ngx_http_wasm_abi_copy_bytes(ngx_http_request_t *r,
                                              ngx_str_t *dst,
                                              const u_char *data,
                                              size_t len);
static void ngx_http_wasm_abi_write_u64_le(u_char *buf, uint64_t value);
static ngx_int_t ngx_http_wasm_abi_require(ngx_http_wasm_abi_ctx_t *ctx,
                                           ngx_uint_t capability);
static ngx_log_t *ngx_http_wasm_abi_log_target(ngx_http_wasm_abi_ctx_t *ctx);
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
static ngx_http_wasm_ctx_t *
ngx_http_wasm_abi_request_ctx(ngx_http_wasm_abi_ctx_t *ctx);
static ngx_http_wasm_subrequest_state_t *
ngx_http_wasm_abi_subrequest_state(ngx_http_wasm_abi_ctx_t *ctx);
static ngx_int_t ngx_http_wasm_abi_subrequest_copy(ngx_http_request_t *r,
                                                   ngx_str_t *dst,
                                                   const u_char *src,
                                                   size_t len);
static ngx_int_t ngx_http_wasm_abi_subrequest_set_runtime_header(
    ngx_http_request_t *r, const ngx_str_t *name, const ngx_str_t *value);
static ngx_int_t
ngx_http_wasm_abi_subrequest_clone_headers(ngx_http_request_t *dst,
                                           ngx_http_request_t *src);
static ngx_int_t ngx_http_wasm_abi_subrequest_method_name(ngx_uint_t method,
                                                          ngx_str_t *name);
static ngx_int_t ngx_http_wasm_abi_subrequest_done(ngx_http_request_t *r,
                                                   void *data,
                                                   ngx_int_t rc);

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

static void ngx_http_wasm_abi_write_u64_le(u_char *buf, uint64_t value) {
    buf[0] = (u_char)(value & 0xff);
    buf[1] = (u_char)((value >> 8) & 0xff);
    buf[2] = (u_char)((value >> 16) & 0xff);
    buf[3] = (u_char)((value >> 24) & 0xff);
    buf[4] = (u_char)((value >> 32) & 0xff);
    buf[5] = (u_char)((value >> 40) & 0xff);
    buf[6] = (u_char)((value >> 48) & 0xff);
    buf[7] = (u_char)((value >> 56) & 0xff);
}

void ngx_http_wasm_abi_init(ngx_http_wasm_abi_ctx_t *ctx,
                            ngx_http_request_t *r,
#if (NGX_HTTP_SSL)
                            ngx_ssl_conn_t *ssl_conn,
#endif
                            ngx_connection_t *c,
                            ngx_http_wasm_shm_zone_t *shm_zone,
                            ngx_http_wasm_metrics_zone_t *metrics_zone,
                            ngx_uint_t capabilities) {
    ngx_memzero(ctx, sizeof(*ctx));

    ctx->request = r;
#if (NGX_HTTP_SSL)
    ctx->ssl_conn = ssl_conn;
#endif
    ctx->connection = c;
    ctx->shm_zone = shm_zone;
    ctx->metrics_zone = metrics_zone;
    ctx->abi_version = NGX_HTTP_WASM_ABI_VERSION;
    ctx->capabilities = capabilities;
    ctx->status = NGX_HTTP_OK;
    ctx->ssl_handshake_alert = SSL_AD_HANDSHAKE_FAILURE;
}

static ngx_int_t ngx_http_wasm_abi_require(ngx_http_wasm_abi_ctx_t *ctx,
                                           ngx_uint_t capability) {
    if ((ctx->capabilities & capability) == 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    return NGX_HTTP_WASM_OK;
}

static ngx_log_t *ngx_http_wasm_abi_log_target(ngx_http_wasm_abi_ctx_t *ctx) {
    if (ctx->request != NULL) {
        return ctx->request->connection->log;
    }

    if (ctx->connection != NULL) {
        return ctx->connection->log;
    }

    return ngx_cycle->log;
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
    if (level > NGX_LOG_DEBUG) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (len > NGX_HTTP_WASM_LOG_MAX_MESSAGE_LEN) {
        len = NGX_HTTP_WASM_LOG_MAX_MESSAGE_LEN;
    }

    ngx_log_error(level,
                  ngx_http_wasm_abi_log_target(ctx),
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

static ngx_http_wasm_ctx_t *
ngx_http_wasm_abi_request_ctx(ngx_http_wasm_abi_ctx_t *ctx) {
    if (ctx == NULL || ctx->request == NULL) {
        return NULL;
    }

    return ngx_http_get_module_ctx(ctx->request, ngx_http_wasm_module);
}

static ngx_http_wasm_subrequest_state_t *
ngx_http_wasm_abi_subrequest_state(ngx_http_wasm_abi_ctx_t *ctx) {
    ngx_http_wasm_ctx_t *wctx;

    wctx = ngx_http_wasm_abi_request_ctx(ctx);
    if (wctx == NULL) {
        return NULL;
    }

    wctx->subrequest.request = ctx->request;

    return &wctx->subrequest;
}

static ngx_int_t ngx_http_wasm_abi_subrequest_copy(ngx_http_request_t *r,
                                                   ngx_str_t *dst,
                                                   const u_char *src,
                                                   size_t len) {
    return ngx_http_wasm_abi_copy_bytes(r, dst, src, len);
}

static ngx_int_t ngx_http_wasm_abi_subrequest_set_runtime_header(
    ngx_http_request_t *r, const ngx_str_t *name, const ngx_str_t *value) {
    ngx_table_elt_t *h;
    u_char *lowcase_key;
    ngx_uint_t hash;

    h = ngx_http_wasm_abi_find_header(r, name->data, name->len);
    if (h == NULL) {
        h = ngx_list_push(&r->headers_in.headers);
        if (h == NULL) {
            return NGX_HTTP_WASM_ERROR;
        }

        ngx_memzero(h, sizeof(*h));

        if (ngx_http_wasm_abi_copy_bytes(r, &h->key, name->data, name->len) !=
            NGX_HTTP_WASM_OK) {
            return NGX_HTTP_WASM_ERROR;
        }

        lowcase_key = ngx_pnalloc(r->pool, name->len);
        if (lowcase_key == NULL) {
            return NGX_HTTP_WASM_ERROR;
        }

        hash = ngx_hash_strlow(lowcase_key, h->key.data, name->len);
        h->hash = hash;
        h->lowcase_key = lowcase_key;
    }

    if (ngx_http_wasm_abi_copy_bytes(r, &h->value, value->data, value->len) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return NGX_HTTP_WASM_OK;
}

static ngx_int_t
ngx_http_wasm_abi_subrequest_clone_headers(ngx_http_request_t *dst,
                                           ngx_http_request_t *src) {
    ngx_list_part_t *part;
    ngx_table_elt_t *h;
    ngx_table_elt_t *copy;
    ngx_uint_t i;

    if (ngx_list_init(
            &dst->headers_in.headers, dst->pool, 8, sizeof(ngx_table_elt_t)) !=
        NGX_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    part = &src->headers_in.headers.part;
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

        copy = ngx_list_push(&dst->headers_in.headers);
        if (copy == NULL) {
            return NGX_HTTP_WASM_ERROR;
        }

        ngx_memzero(copy, sizeof(*copy));
        copy->hash = h[i].hash;

        if (ngx_http_wasm_abi_copy_bytes(
                dst, &copy->key, h[i].key.data, h[i].key.len) !=
            NGX_HTTP_WASM_OK) {
            return NGX_HTTP_WASM_ERROR;
        }

        if (ngx_http_wasm_abi_copy_bytes(
                dst, &copy->value, h[i].value.data, h[i].value.len) !=
            NGX_HTTP_WASM_OK) {
            return NGX_HTTP_WASM_ERROR;
        }

        if (h[i].lowcase_key != NULL) {
            copy->lowcase_key = ngx_pnalloc(dst->pool, h[i].key.len);
            if (copy->lowcase_key == NULL) {
                return NGX_HTTP_WASM_ERROR;
            }

            ngx_memcpy(copy->lowcase_key, h[i].lowcase_key, h[i].key.len);
        }
    }

    return NGX_HTTP_WASM_OK;
}

static ngx_int_t ngx_http_wasm_abi_subrequest_method_name(ngx_uint_t method,
                                                          ngx_str_t *name) {
    switch (method) {
        case NGX_HTTP_GET:
            ngx_str_set(name, "GET");
            return NGX_OK;
        case NGX_HTTP_HEAD:
            ngx_str_set(name, "HEAD");
            return NGX_OK;
        case NGX_HTTP_POST:
            ngx_str_set(name, "POST");
            return NGX_OK;
        case NGX_HTTP_PUT:
            ngx_str_set(name, "PUT");
            return NGX_OK;
        case NGX_HTTP_DELETE:
            ngx_str_set(name, "DELETE");
            return NGX_OK;
        case NGX_HTTP_PATCH:
            ngx_str_set(name, "PATCH");
            return NGX_OK;
        case NGX_HTTP_OPTIONS:
            ngx_str_set(name, "OPTIONS");
            return NGX_OK;
    }

    return NGX_ERROR;
}

static ngx_int_t ngx_http_wasm_abi_subrequest_done(ngx_http_request_t *r,
                                                   void *data,
                                                   ngx_int_t rc) {
    ngx_http_wasm_subrequest_state_t *state = data;
    ngx_buf_t *buf;
    size_t len;

    if (state == NULL) {
        return rc;
    }

    state->subrequest = r;
    state->rc = rc;
    state->done = 1;
    state->active = 0;

    if (!state->capture_body || state->body_limit == 0) {
        return rc;
    }

    if (r->out == NULL || r->out->buf == NULL) {
        return rc;
    }

    buf = r->out->buf;
    len = (size_t)(buf->last - buf->pos);
    if (len > state->body_limit) {
        state->rc = NGX_ERROR;
    }

    return rc;
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

static ngx_http_variable_t *ngx_http_wasm_abi_find_variable(
    ngx_http_request_t *r, const u_char *name, size_t name_len) {
    ngx_http_core_main_conf_t *cmcf;
    ngx_http_variable_t *v, *prefix;
    ngx_uint_t i;
    ngx_str_t var_name;
    ngx_uint_t hash;
    u_char *lowcase;

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
    if (cmcf == NULL) {
        return NULL;
    }

    lowcase = ngx_pnalloc(r->pool, name_len);
    if (lowcase == NULL) {
        return NULL;
    }

    hash = ngx_hash_strlow(lowcase, (u_char *)name, name_len);
    var_name.data = lowcase;
    var_name.len = name_len;

    v = ngx_hash_find(&cmcf->variables_hash, hash, var_name.data, var_name.len);
    if (v != NULL) {
        return v;
    }

    prefix = cmcf->prefix_variables.elts;
    if (prefix == NULL) {
        return NULL;
    }

    for (i = 0; i < cmcf->prefix_variables.nelts; i++) {
        if (prefix[i].name.len == name_len &&
            ngx_strncmp(prefix[i].name.data, var_name.data, name_len) == 0) {
            return &prefix[i];
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

ngx_int_t ngx_http_wasm_abi_resp_get_status(ngx_http_wasm_abi_ctx_t *ctx) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_RESP_STATUS_GET) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return ctx->request->headers_out.status;
}

ngx_int_t ngx_http_wasm_abi_shm_get(ngx_http_wasm_abi_ctx_t *ctx,
                                    const u_char *key,
                                    size_t key_len,
                                    u_char *buf,
                                    size_t buf_len) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_SHARED_KV) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return ngx_http_wasm_shm_get(ctx->shm_zone, key, key_len, buf, buf_len);
}

ngx_int_t ngx_http_wasm_abi_shm_exists(ngx_http_wasm_abi_ctx_t *ctx,
                                       const u_char *key,
                                       size_t key_len) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_SHARED_KV) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return ngx_http_wasm_shm_exists(ctx->shm_zone, key, key_len);
}

ngx_int_t ngx_http_wasm_abi_shm_incr(ngx_http_wasm_abi_ctx_t *ctx,
                                     const u_char *key,
                                     size_t key_len,
                                     ngx_int_t delta) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_SHARED_KV) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return ngx_http_wasm_shm_incr(ctx->shm_zone, key, key_len, delta);
}

ngx_int_t ngx_http_wasm_abi_shm_set(ngx_http_wasm_abi_ctx_t *ctx,
                                    const u_char *key,
                                    size_t key_len,
                                    const u_char *value,
                                    size_t value_len) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_SHARED_KV) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return ngx_http_wasm_shm_set(ctx->shm_zone, key, key_len, value, value_len);
}

ngx_int_t ngx_http_wasm_abi_shm_set_ex(ngx_http_wasm_abi_ctx_t *ctx,
                                       const u_char *key,
                                       size_t key_len,
                                       const u_char *value,
                                       size_t value_len,
                                       ngx_msec_t ttl_msec) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_SHARED_KV) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return ngx_http_wasm_shm_set_ex(
        ctx->shm_zone, key, key_len, value, value_len, ttl_msec);
}

ngx_int_t ngx_http_wasm_abi_shm_add(ngx_http_wasm_abi_ctx_t *ctx,
                                    const u_char *key,
                                    size_t key_len,
                                    const u_char *value,
                                    size_t value_len) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_SHARED_KV) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return ngx_http_wasm_shm_add(ctx->shm_zone, key, key_len, value, value_len);
}

ngx_int_t ngx_http_wasm_abi_shm_replace(ngx_http_wasm_abi_ctx_t *ctx,
                                        const u_char *key,
                                        size_t key_len,
                                        const u_char *value,
                                        size_t value_len) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_SHARED_KV) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return ngx_http_wasm_shm_replace(
        ctx->shm_zone, key, key_len, value, value_len);
}

ngx_int_t ngx_http_wasm_abi_shm_delete(ngx_http_wasm_abi_ctx_t *ctx,
                                       const u_char *key,
                                       size_t key_len) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_SHARED_KV) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return ngx_http_wasm_shm_delete(ctx->shm_zone, key, key_len);
}

ngx_int_t ngx_http_wasm_abi_metric_counter_inc(ngx_http_wasm_abi_ctx_t *ctx,
                                               const u_char *name,
                                               size_t name_len,
                                               ngx_int_t delta) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_METRICS) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return ngx_http_wasm_metrics_counter_inc(
        ctx->metrics_zone, name, name_len, delta);
}

ngx_int_t ngx_http_wasm_abi_metric_gauge_set(ngx_http_wasm_abi_ctx_t *ctx,
                                             const u_char *name,
                                             size_t name_len,
                                             ngx_int_t value) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_METRICS) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return ngx_http_wasm_metrics_gauge_set(
        ctx->metrics_zone, name, name_len, value);
}

ngx_int_t ngx_http_wasm_abi_metric_gauge_add(ngx_http_wasm_abi_ctx_t *ctx,
                                             const u_char *name,
                                             size_t name_len,
                                             ngx_int_t delta) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_METRICS) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return ngx_http_wasm_metrics_gauge_add(
        ctx->metrics_zone, name, name_len, delta);
}

ngx_int_t ngx_http_wasm_abi_balancer_set_peer(ngx_http_wasm_abi_ctx_t *ctx,
                                              ngx_uint_t peer_index) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_BALANCER) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (ctx->request == NULL || ctx->request->upstream == NULL) {
        return NGX_HTTP_WASM_ERROR;
    }

    ctx->balancer_peer_set = 1;
    ctx->balancer_peer_index = peer_index;

    return NGX_HTTP_WASM_OK;
}

#if (NGX_HTTP_SSL)
ngx_int_t ngx_http_wasm_abi_ssl_get_server_name(ngx_http_wasm_abi_ctx_t *ctx,
                                                u_char *buf,
                                                size_t buf_len) {
    const char *servername;
    size_t copy_len, len;

    if (ngx_http_wasm_abi_require(ctx,
                                  NGX_HTTP_WASM_ABI_CAP_SSL_SERVER_NAME_GET) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (ctx->ssl_conn == NULL) {
        return NGX_HTTP_WASM_NOT_FOUND;
    }

    servername = SSL_get_servername(ctx->ssl_conn, TLSEXT_NAMETYPE_host_name);
    if (servername == NULL) {
        return NGX_HTTP_WASM_NOT_FOUND;
    }

    len = ngx_strlen(servername);
    copy_len = ngx_min(len, buf_len);
    if (copy_len != 0) {
        ngx_memcpy(buf, servername, copy_len);
    }

    return (ngx_int_t)len;
}

ngx_int_t ngx_http_wasm_abi_ssl_reject_handshake(ngx_http_wasm_abi_ctx_t *ctx,
                                                 ngx_int_t alert) {
    if (ngx_http_wasm_abi_require(ctx,
                                  NGX_HTTP_WASM_ABI_CAP_SSL_HANDSHAKE_REJECT) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    ctx->ssl_handshake_rejected = 1;
    ctx->ssl_handshake_alert = alert;

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_abi_ssl_set_certificate(ngx_http_wasm_abi_ctx_t *ctx,
                                                const u_char *cert,
                                                size_t cert_len,
                                                const u_char *key,
                                                size_t key_len) {
    BIO *cert_bio, *key_bio;
    X509 *x509;
    EVP_PKEY *pkey;

    if (ngx_http_wasm_abi_require(ctx,
                                  NGX_HTTP_WASM_ABI_CAP_SSL_CERTIFICATE_SET) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (ctx->ssl_conn == NULL || cert_len == 0 || key_len == 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    cert_bio = BIO_new_mem_buf((void *)cert, (int)cert_len);
    if (cert_bio == NULL) {
        return NGX_HTTP_WASM_ERROR;
    }

    x509 = PEM_read_bio_X509_AUX(cert_bio, NULL, NULL, NULL);
    BIO_free(cert_bio);
    if (x509 == NULL) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (SSL_use_certificate(ctx->ssl_conn, x509) != 1) {
        X509_free(x509);
        return NGX_HTTP_WASM_ERROR;
    }

    X509_free(x509);

    key_bio = BIO_new_mem_buf((void *)key, (int)key_len);
    if (key_bio == NULL) {
        return NGX_HTTP_WASM_ERROR;
    }

    pkey = PEM_read_bio_PrivateKey(key_bio, NULL, NULL, NULL);
    BIO_free(key_bio);
    if (pkey == NULL) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (SSL_use_PrivateKey(ctx->ssl_conn, pkey) != 1) {
        EVP_PKEY_free(pkey);
        return NGX_HTTP_WASM_ERROR;
    }

    EVP_PKEY_free(pkey);

    if (SSL_check_private_key(ctx->ssl_conn) != 1) {
        return NGX_HTTP_WASM_ERROR;
    }

    return NGX_HTTP_WASM_OK;
}
#endif

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

ngx_int_t ngx_http_wasm_abi_var_get(ngx_http_wasm_abi_ctx_t *ctx,
                                    const u_char *name,
                                    size_t name_len,
                                    u_char *buf,
                                    size_t buf_len) {
    ngx_http_request_t *r;
    ngx_http_variable_value_t *vv;
    ngx_str_t var_name;
    size_t copy_len;
    ngx_uint_t hash;
    u_char *lowcase;

    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_VAR_GET) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (ctx->request == NULL || name_len == 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    r = ctx->request;
    lowcase = ngx_pnalloc(r->pool, name_len);
    if (lowcase == NULL) {
        return NGX_HTTP_WASM_ERROR;
    }

    hash = ngx_hash_strlow(lowcase, (u_char *)name, name_len);
    var_name.data = lowcase;
    var_name.len = name_len;

    vv = ngx_http_get_variable(r, &var_name, hash);
    if (vv == NULL) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (vv->not_found) {
        return NGX_HTTP_WASM_NOT_FOUND;
    }

    copy_len = ngx_min(vv->len, buf_len);
    if (copy_len != 0) {
        ngx_memcpy(buf, vv->data, copy_len);
    }

    return (ngx_int_t)vv->len;
}

ngx_int_t ngx_http_wasm_abi_var_set(ngx_http_wasm_abi_ctx_t *ctx,
                                    const u_char *name,
                                    size_t name_len,
                                    const u_char *value,
                                    size_t value_len) {
    ngx_http_request_t *r;
    ngx_http_variable_t *var;
    ngx_http_variable_value_t vv;
    ngx_str_t copied;

    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_VAR_SET) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (ctx->request == NULL || name_len == 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    r = ctx->request;
    var = ngx_http_wasm_abi_find_variable(r, name, name_len);
    if (var == NULL) {
        return NGX_HTTP_WASM_NOT_FOUND;
    }

    if ((var->flags & NGX_HTTP_VAR_CHANGEABLE) == 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (ngx_http_wasm_abi_copy_bytes(r, &copied, value, value_len) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    ngx_memzero(&vv, sizeof(vv));
    vv.len = copied.len;
    vv.valid = 1;
    vv.no_cacheable = 0;
    vv.not_found = 0;
    vv.data = copied.data;

    if (var->set_handler != NULL) {
        var->set_handler(r, &vv, var->data);
        return NGX_HTTP_WASM_OK;
    }

    if ((var->flags & NGX_HTTP_VAR_INDEXED) == 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    r->variables[var->index] = vv;

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_abi_time_unix_ms(ngx_http_wasm_abi_ctx_t *ctx,
                                         u_char *buf,
                                         size_t buf_len) {
    ngx_time_t *tp;
    uint64_t value;

    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_TIME) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (buf == NULL || buf_len < sizeof(uint64_t)) {
        return NGX_HTTP_WASM_ERROR;
    }

    tp = ngx_timeofday();
    value = (uint64_t)tp->sec * 1000 + tp->msec;
    ngx_http_wasm_abi_write_u64_le(buf, value);

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_abi_time_monotonic_ms(ngx_http_wasm_abi_ctx_t *ctx,
                                              u_char *buf,
                                              size_t buf_len) {
    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_TIME) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (buf == NULL || buf_len < sizeof(uint64_t)) {
        return NGX_HTTP_WASM_ERROR;
    }

    ngx_http_wasm_abi_write_u64_le(buf, (uint64_t)ngx_current_msec);

    return NGX_HTTP_WASM_OK;
}

ngx_int_t
ngx_http_wasm_abi_resp_set_body_chunk_input(ngx_http_wasm_abi_ctx_t *ctx,
                                            const u_char *data,
                                            size_t len,
                                            ngx_uint_t eof) {
    if (ngx_http_wasm_abi_set_str(ctx, &ctx->resp_body_chunk, data, len, 0) !=
        NGX_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    ctx->resp_body_chunk_set = 1;
    ctx->resp_body_chunk_eof = eof;
    ctx->resp_body_chunk_output_set = 0;
    ctx->resp_body_chunk_output.data = NULL;
    ctx->resp_body_chunk_output.len = 0;

    return NGX_HTTP_WASM_OK;
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

ngx_int_t ngx_http_wasm_abi_resp_get_body_chunk(ngx_http_wasm_abi_ctx_t *ctx,
                                                u_char *buf,
                                                size_t buf_len) {
    size_t copy_len;

    if (ngx_http_wasm_abi_require(ctx,
                                  NGX_HTTP_WASM_ABI_CAP_RESP_BODY_CHUNK_READ) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (!ctx->resp_body_chunk_set) {
        return NGX_HTTP_WASM_NOT_FOUND;
    }

    copy_len = ngx_min(ctx->resp_body_chunk.len, buf_len);
    if (copy_len != 0) {
        ngx_memcpy(buf, ctx->resp_body_chunk.data, copy_len);
    }

    return (ngx_int_t)ctx->resp_body_chunk.len;
}

ngx_int_t
ngx_http_wasm_abi_resp_get_body_chunk_eof(ngx_http_wasm_abi_ctx_t *ctx) {
    if (ngx_http_wasm_abi_require(ctx,
                                  NGX_HTTP_WASM_ABI_CAP_RESP_BODY_CHUNK_READ) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (!ctx->resp_body_chunk_set) {
        return NGX_HTTP_WASM_NOT_FOUND;
    }

    return ctx->resp_body_chunk_eof ? 1 : 0;
}

ngx_int_t ngx_http_wasm_abi_resp_set_body_chunk(ngx_http_wasm_abi_ctx_t *ctx,
                                                const u_char *data,
                                                size_t len) {
    if (ngx_http_wasm_abi_require(
            ctx, NGX_HTTP_WASM_ABI_CAP_RESP_BODY_CHUNK_WRITE) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (ngx_http_wasm_abi_set_str(
            ctx, &ctx->resp_body_chunk_output, data, len, 1) != NGX_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    ctx->resp_body_chunk_output_set = 1;

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

ngx_int_t ngx_http_wasm_abi_subrequest_set_header(ngx_http_wasm_abi_ctx_t *ctx,
                                                  const u_char *name,
                                                  size_t name_len,
                                                  const u_char *value,
                                                  size_t value_len) {
    ngx_http_wasm_subrequest_state_t *state;
    ngx_http_wasm_subrequest_header_t *header;

    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_SUBREQUEST) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (name_len == 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    state = ngx_http_wasm_abi_subrequest_state(ctx);
    if (state == NULL || state->request == NULL || state->active) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (state->pending_headers == NULL) {
        state->pending_headers = ngx_array_create(
            state->request->pool, 2, sizeof(ngx_http_wasm_subrequest_header_t));
        if (state->pending_headers == NULL) {
            return NGX_HTTP_WASM_ERROR;
        }
    }

    header = ngx_array_push(state->pending_headers);
    if (header == NULL) {
        return NGX_HTTP_WASM_ERROR;
    }

    ngx_memzero(header, sizeof(*header));

    if (ngx_http_wasm_abi_subrequest_copy(
            state->request, &header->name, name, name_len) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (ngx_http_wasm_abi_subrequest_copy(
            state->request, &header->value, value, value_len) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_abi_subrequest(ngx_http_wasm_abi_ctx_t *ctx,
                                       const u_char *uri,
                                       size_t uri_len,
                                       const u_char *args,
                                       size_t args_len,
                                       ngx_int_t method,
                                       ngx_uint_t options) {
    ngx_http_wasm_subrequest_state_t *state;
    ngx_http_request_t *r, *sr;
    ngx_http_post_subrequest_t *ps;
    ngx_http_wasm_conf_t *wcf;
    ngx_str_t uri_str, args_str, method_name;
    ngx_uint_t flags;
    ngx_uint_t i;
    ngx_http_wasm_subrequest_header_t *headers;

    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_SUBREQUEST) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (ctx->request == NULL || uri_len == 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    state = ngx_http_wasm_abi_subrequest_state(ctx);
    if (state == NULL || state->request == NULL || state->active) {
        return NGX_HTTP_WASM_ERROR;
    }

    r = state->request;
    wcf = ngx_http_get_module_loc_conf(r, ngx_http_wasm_module);
    if (wcf == NULL) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (ngx_http_wasm_abi_subrequest_copy(r, &uri_str, uri, uri_len) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (args_len != 0) {
        if (ngx_http_wasm_abi_subrequest_copy(r, &args_str, args, args_len) !=
            NGX_HTTP_WASM_OK) {
            return NGX_HTTP_WASM_ERROR;
        }
    } else {
        ngx_str_null(&args_str);
    }

    if (method == 0) {
        method = NGX_HTTP_GET;
    }

    if (ngx_http_wasm_abi_subrequest_method_name((ngx_uint_t)method,
                                                 &method_name) != NGX_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    ps = ngx_pcalloc(r->pool, sizeof(*ps));
    if (ps == NULL) {
        return NGX_HTTP_WASM_ERROR;
    }

    ps->handler = ngx_http_wasm_abi_subrequest_done;
    ps->data = state;

    flags = NGX_HTTP_SUBREQUEST_WAITED;
    if ((options & NGX_HTTP_WASM_SUBREQ_CAPTURE_BODY) != 0) {
        flags |= NGX_HTTP_SUBREQUEST_IN_MEMORY;
    }

    state->capture_body = (options & NGX_HTTP_WASM_SUBREQ_CAPTURE_BODY) != 0;
    state->done = 0;
    state->active = 1;
    state->rc = NGX_OK;
    state->subrequest = NULL;
    state->body_limit = wcf->subrequest_buffer_size;

    if (ngx_http_subrequest(
            r, &uri_str, args_len != 0 ? &args_str : NULL, &sr, ps, flags) !=
        NGX_OK) {
        state->active = 0;
        return NGX_HTTP_WASM_ERROR;
    }

    state->subrequest = sr;

    sr->method = (ngx_uint_t)method;
    sr->method_name = method_name;

    if (!state->capture_body) {
        sr->header_only = 1;
    }

    if (state->pending_headers != NULL && state->pending_headers->nelts != 0) {
        if (ngx_http_wasm_abi_subrequest_clone_headers(sr, r) !=
            NGX_HTTP_WASM_OK) {
            state->active = 0;
            return NGX_HTTP_WASM_ERROR;
        }

        headers = state->pending_headers->elts;
        for (i = 0; i < state->pending_headers->nelts; i++) {
            if (ngx_http_wasm_abi_subrequest_set_runtime_header(
                    sr, &headers[i].name, &headers[i].value) !=
                NGX_HTTP_WASM_OK) {
                state->active = 0;
                return NGX_HTTP_WASM_ERROR;
            }
        }
        state->pending_headers->nelts = 0;
    }

    return NGX_HTTP_WASM_OK;
}

ngx_int_t
ngx_http_wasm_abi_subrequest_get_status(ngx_http_wasm_abi_ctx_t *ctx) {
    ngx_http_wasm_subrequest_state_t *state;

    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_SUBREQUEST) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    state = ngx_http_wasm_abi_subrequest_state(ctx);
    if (state == NULL || !state->done || state->subrequest == NULL ||
        state->rc != NGX_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    return state->subrequest->headers_out.status;
}

ngx_int_t ngx_http_wasm_abi_subrequest_get_header(ngx_http_wasm_abi_ctx_t *ctx,
                                                  const u_char *name,
                                                  size_t name_len,
                                                  u_char *buf,
                                                  size_t buf_len) {
    ngx_http_wasm_subrequest_state_t *state;
    ngx_http_request_t *r;
    ngx_table_elt_t *h;
    ngx_table_elt_t **slot;
    ngx_str_t value;
    size_t copy_len;

    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_SUBREQUEST) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (name_len == 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    state = ngx_http_wasm_abi_subrequest_state(ctx);
    if (state == NULL || !state->done || state->subrequest == NULL ||
        state->rc != NGX_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    r = state->subrequest;
    if (ngx_http_wasm_abi_header_eq(name, name_len, "content-type")) {
        if (r->headers_out.content_type.len == 0) {
            return NGX_HTTP_WASM_NOT_FOUND;
        }

        value = r->headers_out.content_type;
        copy_len = ngx_min(value.len, buf_len);
        if (copy_len != 0) {
            ngx_memcpy(buf, value.data, copy_len);
        }

        return (ngx_int_t)value.len;
    }

    slot = ngx_http_wasm_abi_resp_header_slot(&r->headers_out, name, name_len);
    if (slot != NULL && *slot != NULL) {
        value = (*slot)->value;
        copy_len = ngx_min(value.len, buf_len);
        if (copy_len != 0) {
            ngx_memcpy(buf, value.data, copy_len);
        }

        return (ngx_int_t)value.len;
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

ngx_int_t ngx_http_wasm_abi_subrequest_get_body(ngx_http_wasm_abi_ctx_t *ctx,
                                                u_char *buf,
                                                size_t buf_len) {
    ngx_http_wasm_subrequest_state_t *state;
    ngx_buf_t *body;
    size_t len, copy_len;

    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_SUBREQUEST) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    state = ngx_http_wasm_abi_subrequest_state(ctx);
    if (state == NULL || !state->done || state->subrequest == NULL ||
        state->rc != NGX_OK || !state->capture_body) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (state->subrequest->out == NULL || state->subrequest->out->buf == NULL) {
        return 0;
    }

    body = state->subrequest->out->buf;
    len = (size_t)(body->last - body->pos);
    copy_len = ngx_min(len, buf_len);
    if (copy_len != 0) {
        ngx_memcpy(buf, body->pos, copy_len);
    }

    return (ngx_int_t)len;
}

ngx_int_t
ngx_http_wasm_abi_subrequest_get_body_len(ngx_http_wasm_abi_ctx_t *ctx) {
    ngx_http_wasm_subrequest_state_t *state;

    if (ngx_http_wasm_abi_require(ctx, NGX_HTTP_WASM_ABI_CAP_SUBREQUEST) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_ERROR;
    }

    state = ngx_http_wasm_abi_subrequest_state(ctx);
    if (state == NULL || !state->done || state->subrequest == NULL ||
        state->rc != NGX_OK || !state->capture_body) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (state->subrequest->out == NULL || state->subrequest->out->buf == NULL) {
        return 0;
    }

    return (ngx_int_t)(state->subrequest->out->buf->last -
                       state->subrequest->out->buf->pos);
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
