#ifndef _NGX_WASM_GUEST_ABI_H_INCLUDED_
#define _NGX_WASM_GUEST_ABI_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

#define NGX_WASM_ABI_VERSION 1

#define NGX_WASM_OK 0
#define NGX_WASM_NOT_FOUND -1
#define NGX_WASM_ERROR -2

#define NGX_WASM_LOG_STDERR 0
#define NGX_WASM_LOG_EMERG 1
#define NGX_WASM_LOG_ALERT 2
#define NGX_WASM_LOG_CRIT 3
#define NGX_WASM_LOG_ERR 4
#define NGX_WASM_LOG_WARN 5
#define NGX_WASM_LOG_NOTICE 6
#define NGX_WASM_LOG_INFO 7
#define NGX_WASM_LOG_DEBUG 8

#define NGX_WASM_SUBREQ_CAPTURE_BODY 0x0001

#if defined(__GNUC__) || defined(__clang__)
#define NGX_WASM_EXPORT(name) __attribute__((export_name(name)))
#else
#define NGX_WASM_EXPORT(name)
#endif

int ngx_wasm_log(int level, const void *ptr, int len);
int ngx_wasm_resp_set_status(int status);
int ngx_wasm_resp_get_status(void);
int ngx_wasm_shm_get(const void *key_ptr,
                     int key_len,
                     void *buf_ptr,
                     int buf_len);
int ngx_wasm_shm_set(const void *key_ptr,
                     int key_len,
                     const void *value_ptr,
                     int value_len);
int ngx_wasm_shm_delete(const void *key_ptr, int key_len);
int ngx_wasm_metric_counter_inc(const void *name_ptr, int name_len, int delta);
int ngx_wasm_metric_gauge_set(const void *name_ptr, int name_len, int value);
int ngx_wasm_metric_gauge_add(const void *name_ptr, int name_len, int delta);
int ngx_wasm_balancer_set_peer(int peer_index);
int ngx_wasm_ssl_get_server_name(void *buf_ptr, int buf_len);
int ngx_wasm_ssl_reject_handshake(int alert);
int ngx_wasm_ssl_set_certificate(const void *cert_ptr,
                                 int cert_len,
                                 const void *key_ptr,
                                 int key_len);
int ngx_wasm_req_set_header(const void *name_ptr,
                            int name_len,
                            const void *value_ptr,
                            int value_len);
int ngx_wasm_req_get_header(const void *name_ptr,
                            int name_len,
                            void *buf_ptr,
                            int buf_len);
int ngx_wasm_var_get(const void *name_ptr,
                     int name_len,
                     void *buf_ptr,
                     int buf_len);
int ngx_wasm_var_set(const void *name_ptr,
                     int name_len,
                     const void *value_ptr,
                     int value_len);
int ngx_wasm_time_unix_ms(void *buf_ptr, int buf_len);
int ngx_wasm_time_monotonic_ms(void *buf_ptr, int buf_len);
int ngx_wasm_req_get_body(void *buf_ptr, int buf_len);
int ngx_wasm_subreq_set_header(const void *name_ptr,
                               int name_len,
                               const void *value_ptr,
                               int value_len);
int ngx_wasm_subreq(const void *uri_ptr,
                    int uri_len,
                    const void *args_ptr,
                    int args_len,
                    int method,
                    int options);
int ngx_wasm_subreq_get_status(void);
int ngx_wasm_subreq_get_header(const void *name_ptr,
                               int name_len,
                               void *buf_ptr,
                               int buf_len);
int ngx_wasm_subreq_get_body(void *buf_ptr, int buf_len);
int ngx_wasm_subreq_get_body_len(void);
int ngx_wasm_resp_set_header(const void *name_ptr,
                             int name_len,
                             const void *value_ptr,
                             int value_len);
int ngx_wasm_resp_get_header(const void *name_ptr,
                             int name_len,
                             void *buf_ptr,
                             int buf_len);
int ngx_wasm_resp_write(const void *ptr, int len);
int ngx_wasm_yield(void);

#ifdef __cplusplus
}
#endif

#endif /* _NGX_WASM_GUEST_ABI_H_INCLUDED_ */
