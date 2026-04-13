#ifndef _NGX_HTTP_WASM_RUNTIME_H_INCLUDED_
#define _NGX_HTTP_WASM_RUNTIME_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <ngx_http_wasm_abi.h>

#define NGX_HTTP_WASM_DEFAULT_FUEL_LIMIT 1000000
#define NGX_HTTP_WASM_DEFAULT_TIMESLICE_FUEL 10000
#define NGX_HTTP_WASM_DEFAULT_REQUEST_BODY_BUFFER_SIZE 0

typedef struct ngx_http_wasm_cached_module_s ngx_http_wasm_cached_module_t;
typedef struct ngx_http_wasm_runtime_state_s ngx_http_wasm_runtime_state_t;
typedef struct ngx_http_wasm_resume_state_s ngx_http_wasm_resume_state_t;
typedef struct ngx_http_wasm_phase_conf_s ngx_http_wasm_phase_conf_t;

typedef enum {
    NGX_HTTP_WASM_EXEC_READY = 0,
    NGX_HTTP_WASM_EXEC_RUNNING,
    NGX_HTTP_WASM_EXEC_SUSPENDED,
    NGX_HTTP_WASM_EXEC_DONE,
    NGX_HTTP_WASM_EXEC_ERROR,
} ngx_http_wasm_exec_state_e;

typedef enum {
    NGX_HTTP_WASM_SUSPEND_NONE = 0,
    NGX_HTTP_WASM_SUSPEND_RESCHEDULE,
    NGX_HTTP_WASM_SUSPEND_WAIT_IO,
} ngx_http_wasm_suspend_kind_e;

typedef enum {
    NGX_HTTP_WASM_PHASE_CONTENT = 0,
    NGX_HTTP_WASM_PHASE_REWRITE,
    NGX_HTTP_WASM_PHASE_SERVER_REWRITE,
    NGX_HTTP_WASM_PHASE_HEADER_FILTER,
} ngx_http_wasm_phase_e;

struct ngx_http_wasm_phase_conf_s {
    ngx_flag_t set;
    ngx_http_wasm_cached_module_t *module;
    ngx_str_t module_path;
    ngx_str_t export_name;
};

typedef struct {
    ngx_array_t *modules;
    ngx_http_wasm_runtime_state_t *runtime;
} ngx_http_wasm_main_conf_t;

typedef struct {
    ngx_uint_t fuel_limit;
    ngx_uint_t timeslice_fuel;
    size_t request_body_buffer_size;
    ngx_http_wasm_phase_conf_t content;
    ngx_http_wasm_phase_conf_t rewrite;
    ngx_http_wasm_phase_conf_t server_rewrite;
    ngx_http_wasm_phase_conf_t header_filter;
} ngx_http_wasm_conf_t;

typedef struct {
    ngx_http_request_t *request;
    ngx_http_wasm_phase_conf_t *conf;
    ngx_http_wasm_runtime_state_t *runtime;
    ngx_http_wasm_abi_ctx_t abi;
    uint64_t fuel_limit;
    uint64_t timeslice_fuel;
    uint64_t fuel_remaining;
    ngx_http_wasm_exec_state_e state;
    ngx_http_wasm_suspend_kind_e suspend_kind;
    ngx_http_wasm_phase_e phase_kind;
    /*
     * Request-pool-owned resumable Wasmtime state. This keeps the store and
     * every store-owned handle alive across nginx reposts until request pool
     * cleanup destroys the execution context.
     */
    ngx_http_wasm_resume_state_t *resume_state;
    ngx_uint_t yielded;
} ngx_http_wasm_exec_ctx_t;

ngx_int_t ngx_http_wasm_runtime_init(ngx_conf_t *cf,
                                     ngx_http_wasm_main_conf_t *wmcf);
ngx_http_wasm_cached_module_t *ngx_http_wasm_runtime_get_or_load(
    ngx_conf_t *cf, ngx_http_wasm_main_conf_t *wmcf, ngx_str_t *path);
void ngx_http_wasm_runtime_destroy(ngx_http_wasm_main_conf_t *wmcf);
void ngx_http_wasm_runtime_init_exec_ctx(
    ngx_http_wasm_exec_ctx_t *ctx,
    ngx_http_request_t *r,
    ngx_http_wasm_phase_conf_t *conf,
    ngx_http_wasm_phase_e phase_kind,
    ngx_http_wasm_runtime_state_t *runtime);
void ngx_http_wasm_runtime_cleanup_exec_ctx(ngx_http_wasm_exec_ctx_t *ctx);
ngx_int_t ngx_http_wasm_runtime_run(ngx_http_wasm_exec_ctx_t *ctx);

#endif /* _NGX_HTTP_WASM_RUNTIME_H_INCLUDED_ */
