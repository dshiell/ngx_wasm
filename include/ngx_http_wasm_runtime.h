#ifndef _NGX_HTTP_WASM_RUNTIME_H_INCLUDED_
#define _NGX_HTTP_WASM_RUNTIME_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <ngx_http_wasm_abi.h>

typedef struct {
    ngx_flag_t set;
    ngx_str_t module_path;
    ngx_str_t export_name;
} ngx_http_wasm_conf_t;

typedef struct {
    ngx_http_request_t *request;
    ngx_http_wasm_conf_t *conf;
    ngx_http_wasm_abi_ctx_t abi;
    uint64_t fuel_limit;
} ngx_http_wasm_exec_ctx_t;

ngx_int_t ngx_http_wasm_runtime_init_process(ngx_cycle_t *cycle);
void ngx_http_wasm_runtime_exit_process(ngx_cycle_t *cycle);
void ngx_http_wasm_runtime_init_exec_ctx(ngx_http_wasm_exec_ctx_t *ctx,
                                         ngx_http_request_t *r,
                                         ngx_http_wasm_conf_t *conf);
ngx_int_t ngx_http_wasm_runtime_run(ngx_http_wasm_exec_ctx_t *ctx);

#endif /* _NGX_HTTP_WASM_RUNTIME_H_INCLUDED_ */
