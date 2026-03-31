#ifndef _NGX_HTTP_WASM_ABI_H_INCLUDED_
#define _NGX_HTTP_WASM_ABI_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#define NGX_HTTP_WASM_ABI_VERSION 1

#define NGX_HTTP_WASM_OK 0
#define NGX_HTTP_WASM_ERROR -1

#define NGX_HTTP_WASM_LOG_STDERR NGX_LOG_STDERR
#define NGX_HTTP_WASM_LOG_EMERG NGX_LOG_EMERG
#define NGX_HTTP_WASM_LOG_ALERT NGX_LOG_ALERT
#define NGX_HTTP_WASM_LOG_CRIT NGX_LOG_CRIT
#define NGX_HTTP_WASM_LOG_ERR NGX_LOG_ERR
#define NGX_HTTP_WASM_LOG_WARN NGX_LOG_WARN
#define NGX_HTTP_WASM_LOG_NOTICE NGX_LOG_NOTICE
#define NGX_HTTP_WASM_LOG_INFO NGX_LOG_INFO
#define NGX_HTTP_WASM_LOG_DEBUG NGX_LOG_DEBUG

typedef struct {
    ngx_http_request_t *request;
    ngx_uint_t status_set;
    ngx_uint_t body_set;
    ngx_uint_t body_is_borrowed;
    ngx_uint_t content_type_set;
    ngx_uint_t response_sent;
    ngx_uint_t abi_version;
    ngx_int_t status;
    ngx_str_t body;
    ngx_str_t content_type;
} ngx_http_wasm_abi_ctx_t;

void ngx_http_wasm_abi_init(ngx_http_wasm_abi_ctx_t *ctx,
                            ngx_http_request_t *r);
ngx_int_t ngx_http_wasm_abi_log(ngx_http_wasm_abi_ctx_t *ctx,
                                ngx_uint_t level,
                                const u_char *data,
                                size_t len);
ngx_int_t ngx_http_wasm_abi_resp_set_status(ngx_http_wasm_abi_ctx_t *ctx,
                                            ngx_int_t status);
ngx_int_t ngx_http_wasm_abi_resp_set_content_type(ngx_http_wasm_abi_ctx_t *ctx,
                                                  const u_char *data,
                                                  size_t len,
                                                  ngx_uint_t copy);
ngx_int_t ngx_http_wasm_abi_resp_write(ngx_http_wasm_abi_ctx_t *ctx,
                                       const u_char *data,
                                       size_t len,
                                       ngx_uint_t copy);
ngx_int_t ngx_http_wasm_abi_send_response(ngx_http_wasm_abi_ctx_t *ctx);

#endif /* _NGX_HTTP_WASM_ABI_H_INCLUDED_ */
