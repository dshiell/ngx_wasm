#ifndef _NGX_HTTP_WASM_ABI_H_INCLUDED_
#define _NGX_HTTP_WASM_ABI_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#if (NGX_HTTP_SSL)
#include <ngx_event_openssl.h>
#endif

#include <ngx_http_wasm_shm.h>
#include <ngx_http_wasm_metrics.h>

#define NGX_HTTP_WASM_ABI_VERSION 1

#define NGX_HTTP_WASM_OK 0
#define NGX_HTTP_WASM_NOT_FOUND -1
#define NGX_HTTP_WASM_ERROR -2

#define NGX_HTTP_WASM_LOG_STDERR NGX_LOG_STDERR
#define NGX_HTTP_WASM_LOG_EMERG NGX_LOG_EMERG
#define NGX_HTTP_WASM_LOG_ALERT NGX_LOG_ALERT
#define NGX_HTTP_WASM_LOG_CRIT NGX_LOG_CRIT
#define NGX_HTTP_WASM_LOG_ERR NGX_LOG_ERR
#define NGX_HTTP_WASM_LOG_WARN NGX_LOG_WARN
#define NGX_HTTP_WASM_LOG_NOTICE NGX_LOG_NOTICE
#define NGX_HTTP_WASM_LOG_INFO NGX_LOG_INFO
#define NGX_HTTP_WASM_LOG_DEBUG NGX_LOG_DEBUG

#define NGX_HTTP_WASM_ABI_CAP_REQ_HEADERS_RW 0x0001
#define NGX_HTTP_WASM_ABI_CAP_REQ_HEADERS_RO 0x0002
#define NGX_HTTP_WASM_ABI_CAP_REQ_BODY_GET 0x0004
#define NGX_HTTP_WASM_ABI_CAP_RESP_STATUS_SET 0x0008
#define NGX_HTTP_WASM_ABI_CAP_RESP_HEADERS_RW 0x0010
#define NGX_HTTP_WASM_ABI_CAP_RESP_BODY_WRITE 0x0020
#define NGX_HTTP_WASM_ABI_CAP_YIELD 0x0040
#define NGX_HTTP_WASM_ABI_CAP_RESP_BODY_CHUNK_READ 0x0080
#define NGX_HTTP_WASM_ABI_CAP_RESP_BODY_CHUNK_WRITE 0x0100
#define NGX_HTTP_WASM_ABI_CAP_RESP_STATUS_GET 0x0200
#define NGX_HTTP_WASM_ABI_CAP_SSL_SERVER_NAME_GET 0x0400
#define NGX_HTTP_WASM_ABI_CAP_SSL_HANDSHAKE_REJECT 0x0800
#define NGX_HTTP_WASM_ABI_CAP_SSL_CERTIFICATE_SET 0x1000
#define NGX_HTTP_WASM_ABI_CAP_SHARED_KV 0x2000
#define NGX_HTTP_WASM_ABI_CAP_METRICS 0x4000

typedef struct {
    ngx_http_request_t *request;
    ngx_connection_t *connection;
#if (NGX_HTTP_SSL)
    ngx_ssl_conn_t *ssl_conn;
#endif
    ngx_http_wasm_shm_zone_t *shm_zone;
    ngx_http_wasm_metrics_zone_t *metrics_zone;
    ngx_uint_t status_set;
    ngx_uint_t body_set;
    ngx_uint_t body_is_borrowed;
    ngx_uint_t content_type_set;
    ngx_uint_t response_sent;
    ngx_uint_t request_body_set;
    ngx_uint_t resp_body_chunk_set;
    ngx_uint_t resp_body_chunk_output_set;
    ngx_uint_t resp_body_chunk_eof;
    ngx_uint_t ssl_handshake_rejected;
    ngx_uint_t abi_version;
    ngx_uint_t capabilities;
    ngx_int_t status;
    ngx_int_t ssl_handshake_alert;
    ngx_str_t body;
    ngx_str_t content_type;
    ngx_str_t request_body;
    ngx_str_t resp_body_chunk;
    ngx_str_t resp_body_chunk_output;
} ngx_http_wasm_abi_ctx_t;

void ngx_http_wasm_abi_init(ngx_http_wasm_abi_ctx_t *ctx,
                            ngx_http_request_t *r,
#if (NGX_HTTP_SSL)
                            ngx_ssl_conn_t *ssl_conn,
#endif
                            ngx_connection_t *c,
                            ngx_http_wasm_shm_zone_t *shm_zone,
                            ngx_http_wasm_metrics_zone_t *metrics_zone,
                            ngx_uint_t capabilities);
ngx_int_t ngx_http_wasm_abi_log(ngx_http_wasm_abi_ctx_t *ctx,
                                ngx_uint_t level,
                                const u_char *data,
                                size_t len);
ngx_int_t ngx_http_wasm_abi_resp_set_status(ngx_http_wasm_abi_ctx_t *ctx,
                                            ngx_int_t status);
ngx_int_t ngx_http_wasm_abi_resp_get_status(ngx_http_wasm_abi_ctx_t *ctx);
#if (NGX_HTTP_SSL)
ngx_int_t ngx_http_wasm_abi_ssl_get_server_name(ngx_http_wasm_abi_ctx_t *ctx,
                                                u_char *buf,
                                                size_t buf_len);
ngx_int_t ngx_http_wasm_abi_ssl_reject_handshake(ngx_http_wasm_abi_ctx_t *ctx,
                                                 ngx_int_t alert);
ngx_int_t ngx_http_wasm_abi_ssl_set_certificate(ngx_http_wasm_abi_ctx_t *ctx,
                                                const u_char *cert,
                                                size_t cert_len,
                                                const u_char *key,
                                                size_t key_len);
#endif
ngx_int_t ngx_http_wasm_abi_req_set_header(ngx_http_wasm_abi_ctx_t *ctx,
                                           const u_char *name,
                                           size_t name_len,
                                           const u_char *value,
                                           size_t value_len);
ngx_int_t ngx_http_wasm_abi_req_get_header(ngx_http_wasm_abi_ctx_t *ctx,
                                           const u_char *name,
                                           size_t name_len,
                                           u_char *buf,
                                           size_t buf_len);
ngx_int_t ngx_http_wasm_abi_req_set_body(ngx_http_wasm_abi_ctx_t *ctx,
                                         const u_char *data,
                                         size_t len);
ngx_int_t ngx_http_wasm_abi_req_get_body(ngx_http_wasm_abi_ctx_t *ctx,
                                         u_char *buf,
                                         size_t buf_len);
ngx_int_t
ngx_http_wasm_abi_resp_set_body_chunk_input(ngx_http_wasm_abi_ctx_t *ctx,
                                            const u_char *data,
                                            size_t len,
                                            ngx_uint_t eof);
ngx_int_t ngx_http_wasm_abi_resp_set_header(ngx_http_wasm_abi_ctx_t *ctx,
                                            const u_char *name,
                                            size_t name_len,
                                            const u_char *value,
                                            size_t value_len);
ngx_int_t ngx_http_wasm_abi_resp_get_header(ngx_http_wasm_abi_ctx_t *ctx,
                                            const u_char *name,
                                            size_t name_len,
                                            u_char *buf,
                                            size_t buf_len);
ngx_int_t ngx_http_wasm_abi_resp_set_content_type(ngx_http_wasm_abi_ctx_t *ctx,
                                                  const u_char *data,
                                                  size_t len,
                                                  ngx_uint_t copy);
ngx_int_t ngx_http_wasm_abi_shm_get(ngx_http_wasm_abi_ctx_t *ctx,
                                    const u_char *key,
                                    size_t key_len,
                                    u_char *buf,
                                    size_t buf_len);
ngx_int_t ngx_http_wasm_abi_shm_set(ngx_http_wasm_abi_ctx_t *ctx,
                                    const u_char *key,
                                    size_t key_len,
                                    const u_char *value,
                                    size_t value_len);
ngx_int_t ngx_http_wasm_abi_shm_delete(ngx_http_wasm_abi_ctx_t *ctx,
                                       const u_char *key,
                                       size_t key_len);
ngx_int_t ngx_http_wasm_abi_metric_counter_inc(ngx_http_wasm_abi_ctx_t *ctx,
                                               const u_char *name,
                                               size_t name_len,
                                               ngx_int_t delta);
ngx_int_t ngx_http_wasm_abi_metric_gauge_set(ngx_http_wasm_abi_ctx_t *ctx,
                                             const u_char *name,
                                             size_t name_len,
                                             ngx_int_t value);
ngx_int_t ngx_http_wasm_abi_metric_gauge_add(ngx_http_wasm_abi_ctx_t *ctx,
                                             const u_char *name,
                                             size_t name_len,
                                             ngx_int_t delta);
ngx_int_t ngx_http_wasm_abi_resp_get_body_chunk(ngx_http_wasm_abi_ctx_t *ctx,
                                                u_char *buf,
                                                size_t buf_len);
ngx_int_t
ngx_http_wasm_abi_resp_get_body_chunk_eof(ngx_http_wasm_abi_ctx_t *ctx);
ngx_int_t ngx_http_wasm_abi_resp_set_body_chunk(ngx_http_wasm_abi_ctx_t *ctx,
                                                const u_char *data,
                                                size_t len);
ngx_int_t ngx_http_wasm_abi_resp_write(ngx_http_wasm_abi_ctx_t *ctx,
                                       const u_char *data,
                                       size_t len,
                                       ngx_uint_t copy);
ngx_int_t ngx_http_wasm_abi_send_response(ngx_http_wasm_abi_ctx_t *ctx);

#endif /* _NGX_HTTP_WASM_ABI_H_INCLUDED_ */
