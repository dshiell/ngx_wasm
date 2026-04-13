#ifndef _NGX_HTTP_WASM_MODULE_INT_H_INCLUDED_
#define _NGX_HTTP_WASM_MODULE_INT_H_INCLUDED_

#include <ngx_http_wasm_runtime.h>

typedef struct ngx_http_wasm_ctx_s ngx_http_wasm_ctx_t;

struct ngx_http_wasm_ctx_s {
    ngx_http_wasm_exec_ctx_t exec;
    ngx_http_wasm_exec_ctx_t header_filter_exec;
    ngx_uint_t header_filter_exec_set;
    ngx_uint_t waiting;
    ngx_uint_t request_body_reading;
    ngx_uint_t request_body_ready;
    ngx_uint_t request_body_async;
    ngx_int_t request_body_status;
};

extern ngx_module_t ngx_http_wasm_module;

ngx_int_t ngx_http_wasm_header_filter_init_process(void);
ngx_int_t ngx_http_wasm_header_filter_handler(ngx_http_request_t *r);
ngx_int_t ngx_http_wasm_prepare_request_body(ngx_http_request_t *r,
                                             ngx_http_wasm_conf_t *wcf,
                                             ngx_http_wasm_ctx_t *ctx);
void ngx_http_wasm_resume_handler(ngx_http_request_t *r);
void ngx_http_wasm_cleanup_ctx(void *data);

#endif /* _NGX_HTTP_WASM_MODULE_INT_H_INCLUDED_ */
