#include <ngx_http_wasm_runtime.h>

#include <wasm.h>
#include <wasmtime/config.h>
#include <wasmtime/error.h>
#include <wasmtime/extern.h>
#include <wasmtime/func.h>
#include <wasmtime/instance.h>
#include <wasmtime/async.h>
#include <wasmtime/linker.h>
#include <wasmtime/memory.h>
#include <wasmtime/module.h>
#include <wasmtime/store.h>
#include <wasmtime/trap.h>
#include <wasmtime/val.h>
#include <wasmtime/wat.h>

#define NGX_HTTP_WASM_IMPORT_MODULE "env"
#define NGX_HTTP_WASM_MEMORY_EXPORT "memory"

extern ngx_module_t ngx_http_wasm_module;

struct ngx_http_wasm_cached_module_s {
    ngx_str_t module_path;
    wasmtime_module_t *module;
};

struct ngx_http_wasm_resume_state_s {
    enum {
        NGX_HTTP_WASM_FUTURE_NONE = 0,
        NGX_HTTP_WASM_FUTURE_INSTANTIATE,
        NGX_HTTP_WASM_FUTURE_CALL,
    } future_kind;
    /*
     * The request owns exactly one store for this resume state. The instance
     * and function handles below are borrowed from that store and become
     * invalid immediately when the store is deleted.
     */
    wasmtime_store_t *store;
    wasmtime_instance_t instance;
    wasmtime_func_t func;
    /*
     * At most one future may be live for a store at a time. While it exists,
     * Wasmtime writes trap/error/results into the stable storage below.
     */
    wasmtime_call_future_t *future;
    wasmtime_val_t results[1];
    wasm_trap_t *trap;
    wasmtime_error_t *error;
    unsigned instance_ready : 1;
    unsigned func_ready : 1;
};

struct ngx_http_wasm_runtime_state_s {
    ngx_log_t *log;
    wasm_engine_t *engine;
    wasmtime_linker_t *linker;
};

static ngx_int_t
ngx_http_wasm_runtime_define_host_funcs(ngx_http_wasm_runtime_state_t *rt);
static ngx_int_t
ngx_http_wasm_runtime_define_func(ngx_http_wasm_runtime_state_t *rt,
                                  const char *name,
                                  wasm_functype_t *ty,
                                  wasmtime_func_callback_t cb);
static wasm_functype_t *ngx_http_wasm_runtime_functype_4_1(void);
static wasm_functype_t *ngx_http_wasm_runtime_functype_6_1(void);
static ngx_http_wasm_cached_module_t *
ngx_http_wasm_runtime_find_module(ngx_http_wasm_main_conf_t *wmcf,
                                  const ngx_str_t *path);
static ngx_int_t ngx_http_wasm_runtime_read_file(ngx_pool_t *pool,
                                                 ngx_log_t *log,
                                                 const u_char *path,
                                                 ngx_str_t *out);
static wasmtime_error_t *
ngx_http_wasm_runtime_compile_module(ngx_http_wasm_runtime_state_t *rt,
                                     const ngx_str_t *bytes,
                                     wasmtime_module_t **module);
static void ngx_http_wasm_runtime_log_error(ngx_log_t *log,
                                            const char *context,
                                            wasmtime_error_t *error);
static void ngx_http_wasm_runtime_log_trap(ngx_log_t *log,
                                           const char *context,
                                           wasm_trap_t *trap);
static ngx_int_t
ngx_http_wasm_runtime_update_fuel(ngx_http_wasm_exec_ctx_t *ctx,
                                  wasmtime_context_t *context,
                                  const char *phase);
static ngx_log_t *ngx_http_wasm_runtime_exec_log(ngx_http_wasm_exec_ctx_t *ctx);
static ngx_pool_t *
ngx_http_wasm_runtime_exec_pool(ngx_http_wasm_exec_ctx_t *ctx);
static void ngx_http_wasm_runtime_begin_run(ngx_http_wasm_exec_ctx_t *ctx);
static void ngx_http_wasm_runtime_log_suspend(ngx_http_wasm_exec_ctx_t *ctx,
                                              const char *reason);
static ngx_int_t
ngx_http_wasm_runtime_suspend(ngx_http_wasm_exec_ctx_t *ctx,
                              ngx_http_wasm_suspend_kind_e kind,
                              const char *reason);
static void
ngx_http_wasm_runtime_clear_async_future(ngx_http_wasm_resume_state_t *resume);
static void
ngx_http_wasm_runtime_clear_async_outputs(ngx_http_wasm_resume_state_t *resume);
static void
ngx_http_wasm_runtime_reset_resume_state(ngx_http_wasm_resume_state_t *resume);
static ngx_int_t
ngx_http_wasm_runtime_prepare_async_op(ngx_http_wasm_exec_ctx_t *ctx,
                                       ngx_http_wasm_resume_state_t *resume,
                                       ngx_uint_t future_kind);
static ngx_int_t
ngx_http_wasm_runtime_prepare_async_yield(ngx_http_wasm_exec_ctx_t *ctx,
                                          wasmtime_context_t *context);
static wasm_trap_t *ngx_http_wasm_runtime_get_memory(wasmtime_caller_t *caller,
                                                     uint32_t ptr,
                                                     uint32_t len,
                                                     const u_char **data);
static wasm_trap_t *ngx_http_wasm_runtime_get_memory_mut(
    wasmtime_caller_t *caller, uint32_t ptr, uint32_t len, u_char **data);
static wasm_trap_t *ngx_http_wasm_runtime_bad_signature(const char *name);
static wasm_trap_t *ngx_http_wasm_runtime_phase_forbidden(const char *name);
static wasm_trap_t *ngx_http_wasm_host_log(void *env,
                                           wasmtime_caller_t *caller,
                                           const wasmtime_val_t *args,
                                           size_t nargs,
                                           wasmtime_val_t *results,
                                           size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_resp_set_status(void *env,
                                   wasmtime_caller_t *caller,
                                   const wasmtime_val_t *args,
                                   size_t nargs,
                                   wasmtime_val_t *results,
                                   size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_resp_get_status(void *env,
                                   wasmtime_caller_t *caller,
                                   const wasmtime_val_t *args,
                                   size_t nargs,
                                   wasmtime_val_t *results,
                                   size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_metric_counter_inc(void *env,
                                      wasmtime_caller_t *caller,
                                      const wasmtime_val_t *args,
                                      size_t nargs,
                                      wasmtime_val_t *results,
                                      size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_metric_gauge_set(void *env,
                                    wasmtime_caller_t *caller,
                                    const wasmtime_val_t *args,
                                    size_t nargs,
                                    wasmtime_val_t *results,
                                    size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_metric_gauge_add(void *env,
                                    wasmtime_caller_t *caller,
                                    const wasmtime_val_t *args,
                                    size_t nargs,
                                    wasmtime_val_t *results,
                                    size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_balancer_set_peer(void *env,
                                     wasmtime_caller_t *caller,
                                     const wasmtime_val_t *args,
                                     size_t nargs,
                                     wasmtime_val_t *results,
                                     size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_ssl_get_server_name(void *env,
                                       wasmtime_caller_t *caller,
                                       const wasmtime_val_t *args,
                                       size_t nargs,
                                       wasmtime_val_t *results,
                                       size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_ssl_reject_handshake(void *env,
                                        wasmtime_caller_t *caller,
                                        const wasmtime_val_t *args,
                                        size_t nargs,
                                        wasmtime_val_t *results,
                                        size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_ssl_set_certificate(void *env,
                                       wasmtime_caller_t *caller,
                                       const wasmtime_val_t *args,
                                       size_t nargs,
                                       wasmtime_val_t *results,
                                       size_t nresults);
static wasm_trap_t *ngx_http_wasm_host_shm_get(void *env,
                                               wasmtime_caller_t *caller,
                                               const wasmtime_val_t *args,
                                               size_t nargs,
                                               wasmtime_val_t *results,
                                               size_t nresults);
static wasm_trap_t *ngx_http_wasm_host_shm_set(void *env,
                                               wasmtime_caller_t *caller,
                                               const wasmtime_val_t *args,
                                               size_t nargs,
                                               wasmtime_val_t *results,
                                               size_t nresults);
static wasm_trap_t *ngx_http_wasm_host_shm_delete(void *env,
                                                  wasmtime_caller_t *caller,
                                                  const wasmtime_val_t *args,
                                                  size_t nargs,
                                                  wasmtime_val_t *results,
                                                  size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_req_set_header(void *env,
                                  wasmtime_caller_t *caller,
                                  const wasmtime_val_t *args,
                                  size_t nargs,
                                  wasmtime_val_t *results,
                                  size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_req_get_header(void *env,
                                  wasmtime_caller_t *caller,
                                  const wasmtime_val_t *args,
                                  size_t nargs,
                                  wasmtime_val_t *results,
                                  size_t nresults);
static wasm_trap_t *ngx_http_wasm_host_var_get(void *env,
                                               wasmtime_caller_t *caller,
                                               const wasmtime_val_t *args,
                                               size_t nargs,
                                               wasmtime_val_t *results,
                                               size_t nresults);
static wasm_trap_t *ngx_http_wasm_host_var_set(void *env,
                                               wasmtime_caller_t *caller,
                                               const wasmtime_val_t *args,
                                               size_t nargs,
                                               wasmtime_val_t *results,
                                               size_t nresults);
static wasm_trap_t *ngx_http_wasm_host_time_unix_ms(void *env,
                                                    wasmtime_caller_t *caller,
                                                    const wasmtime_val_t *args,
                                                    size_t nargs,
                                                    wasmtime_val_t *results,
                                                    size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_time_monotonic_ms(void *env,
                                     wasmtime_caller_t *caller,
                                     const wasmtime_val_t *args,
                                     size_t nargs,
                                     wasmtime_val_t *results,
                                     size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_subreq_set_header(void *env,
                                     wasmtime_caller_t *caller,
                                     const wasmtime_val_t *args,
                                     size_t nargs,
                                     wasmtime_val_t *results,
                                     size_t nresults);
static wasm_trap_t *ngx_http_wasm_host_subreq(void *env,
                                              wasmtime_caller_t *caller,
                                              const wasmtime_val_t *args,
                                              size_t nargs,
                                              wasmtime_val_t *results,
                                              size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_subreq_get_status(void *env,
                                     wasmtime_caller_t *caller,
                                     const wasmtime_val_t *args,
                                     size_t nargs,
                                     wasmtime_val_t *results,
                                     size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_subreq_get_header(void *env,
                                     wasmtime_caller_t *caller,
                                     const wasmtime_val_t *args,
                                     size_t nargs,
                                     wasmtime_val_t *results,
                                     size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_subreq_get_body(void *env,
                                   wasmtime_caller_t *caller,
                                   const wasmtime_val_t *args,
                                   size_t nargs,
                                   wasmtime_val_t *results,
                                   size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_subreq_get_body_len(void *env,
                                       wasmtime_caller_t *caller,
                                       const wasmtime_val_t *args,
                                       size_t nargs,
                                       wasmtime_val_t *results,
                                       size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_resp_set_header(void *env,
                                   wasmtime_caller_t *caller,
                                   const wasmtime_val_t *args,
                                   size_t nargs,
                                   wasmtime_val_t *results,
                                   size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_resp_get_header(void *env,
                                   wasmtime_caller_t *caller,
                                   const wasmtime_val_t *args,
                                   size_t nargs,
                                   wasmtime_val_t *results,
                                   size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_resp_get_body_chunk(void *env,
                                       wasmtime_caller_t *caller,
                                       const wasmtime_val_t *args,
                                       size_t nargs,
                                       wasmtime_val_t *results,
                                       size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_resp_get_body_chunk_eof(void *env,
                                           wasmtime_caller_t *caller,
                                           const wasmtime_val_t *args,
                                           size_t nargs,
                                           wasmtime_val_t *results,
                                           size_t nresults);
static wasm_trap_t *
ngx_http_wasm_host_resp_set_body_chunk(void *env,
                                       wasmtime_caller_t *caller,
                                       const wasmtime_val_t *args,
                                       size_t nargs,
                                       wasmtime_val_t *results,
                                       size_t nresults);
static wasm_trap_t *ngx_http_wasm_host_req_get_body(void *env,
                                                    wasmtime_caller_t *caller,
                                                    const wasmtime_val_t *args,
                                                    size_t nargs,
                                                    wasmtime_val_t *results,
                                                    size_t nresults);
static wasm_trap_t *ngx_http_wasm_host_resp_write(void *env,
                                                  wasmtime_caller_t *caller,
                                                  const wasmtime_val_t *args,
                                                  size_t nargs,
                                                  wasmtime_val_t *results,
                                                  size_t nresults);
static wasm_trap_t *ngx_http_wasm_host_yield(void *env,
                                             wasmtime_caller_t *caller,
                                             const wasmtime_val_t *args,
                                             size_t nargs,
                                             wasmtime_val_t *results,
                                             size_t nresults);

ngx_int_t ngx_http_wasm_runtime_init(ngx_conf_t *cf,
                                     ngx_http_wasm_main_conf_t *wmcf) {
    wasm_config_t *config;
    ngx_http_wasm_runtime_state_t *rt;

    if (wmcf->runtime != NULL) {
        return NGX_OK;
    }

    rt = ngx_pcalloc(cf->pool, sizeof(*rt));
    if (rt == NULL) {
        return NGX_ERROR;
    }

    rt->log = cf->log;
    wmcf->runtime = rt;

    config = wasm_config_new();
    if (config == NULL) {
        ngx_log_error(NGX_LOG_ERR,
                      cf->log,
                      0,
                      "ngx_wasm: failed to create Wasmtime config");
        return NGX_ERROR;
    }

    wasmtime_config_consume_fuel_set(config, true);
    wasmtime_config_async_support_set(config, true);
    wasmtime_config_parallel_compilation_set(config, false);
#if (NGX_DARWIN)
    wasmtime_config_macos_use_mach_ports_set(config, false);
#endif

    rt->engine = wasm_engine_new_with_config(config);
    if (rt->engine == NULL) {
        ngx_log_error(NGX_LOG_ERR,
                      cf->log,
                      0,
                      "ngx_wasm: failed to create Wasmtime engine");
        wmcf->runtime = NULL;
        return NGX_ERROR;
    }

    rt->linker = wasmtime_linker_new(rt->engine);
    if (rt->linker == NULL) {
        ngx_log_error(NGX_LOG_ERR,
                      cf->log,
                      0,
                      "ngx_wasm: failed to create Wasmtime linker");
        ngx_http_wasm_runtime_destroy(wmcf);
        wmcf->runtime = NULL;
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_host_funcs(rt) != NGX_OK) {
        ngx_http_wasm_runtime_destroy(wmcf);
        wmcf->runtime = NULL;
        return NGX_ERROR;
    }

    return NGX_OK;
}

void ngx_http_wasm_runtime_destroy(ngx_http_wasm_main_conf_t *wmcf) {
    ngx_http_wasm_cached_module_t **modules;
    ngx_http_wasm_runtime_state_t *rt;
    ngx_uint_t i;

    if (wmcf == NULL || wmcf->runtime == NULL) {
        return;
    }

    rt = wmcf->runtime;

    if (wmcf->modules != NULL) {
        modules = wmcf->modules->elts;
        for (i = 0; i < wmcf->modules->nelts; i++) {
            if (modules[i] != NULL && modules[i]->module != NULL) {
                wasmtime_module_delete(modules[i]->module);
                modules[i]->module = NULL;
            }
        }
    }

    if (rt->linker != NULL) {
        wasmtime_linker_delete(rt->linker);
        rt->linker = NULL;
    }

    if (rt->engine != NULL) {
        wasm_engine_delete(rt->engine);
        rt->engine = NULL;
    }
}

void ngx_http_wasm_runtime_init_exec_ctx(
    ngx_http_wasm_exec_ctx_t *ctx,
    ngx_http_request_t *r,
    ngx_http_wasm_phase_conf_t *conf,
    ngx_http_wasm_phase_e phase_kind,
    ngx_http_wasm_runtime_state_t *runtime) {
    ngx_memzero(ctx, sizeof(*ctx));

    ctx->request = r;
    ctx->connection = r->connection;
#if (NGX_HTTP_SSL)
    ctx->ssl_connection = NULL;
#endif
    ctx->conf = conf;
    ctx->runtime = runtime;
    ctx->fuel_limit = 0;
    ctx->timeslice_fuel = 0;
    ctx->fuel_remaining = 0;
    ctx->state = NGX_HTTP_WASM_EXEC_READY;
    ctx->suspend_kind = NGX_HTTP_WASM_SUSPEND_NONE;
    ctx->phase_kind = phase_kind;
    ctx->resume_state = NULL;
    ctx->yielded = 0;

    ngx_http_wasm_abi_init(
        &ctx->abi,
        r,
#if (NGX_HTTP_SSL)
        NULL,
#endif
        r->connection,
        ((ngx_http_wasm_main_conf_t *)ngx_http_get_module_main_conf(
             r, ngx_http_wasm_module))
            ->shm_zone,
        ((ngx_http_wasm_main_conf_t *)ngx_http_get_module_main_conf(
             r, ngx_http_wasm_module))
            ->metrics_zone,
        NGX_HTTP_WASM_ABI_CAP_REQ_HEADERS_RO |
            NGX_HTTP_WASM_ABI_CAP_REQ_HEADERS_RW |
            NGX_HTTP_WASM_ABI_CAP_VAR_GET | NGX_HTTP_WASM_ABI_CAP_VAR_SET |
            NGX_HTTP_WASM_ABI_CAP_TIME | NGX_HTTP_WASM_ABI_CAP_REQ_BODY_GET |
            NGX_HTTP_WASM_ABI_CAP_RESP_STATUS_SET |
            NGX_HTTP_WASM_ABI_CAP_RESP_STATUS_GET |
            NGX_HTTP_WASM_ABI_CAP_RESP_HEADERS_RW |
            NGX_HTTP_WASM_ABI_CAP_RESP_BODY_WRITE |
            NGX_HTTP_WASM_ABI_CAP_YIELD | NGX_HTTP_WASM_ABI_CAP_SHARED_KV |
            NGX_HTTP_WASM_ABI_CAP_METRICS | NGX_HTTP_WASM_ABI_CAP_SUBREQUEST);

    if (phase_kind == NGX_HTTP_WASM_PHASE_HEADER_FILTER) {
        ctx->abi.capabilities =
            NGX_HTTP_WASM_ABI_CAP_REQ_HEADERS_RO |
            NGX_HTTP_WASM_ABI_CAP_VAR_GET | NGX_HTTP_WASM_ABI_CAP_TIME |
            NGX_HTTP_WASM_ABI_CAP_RESP_STATUS_SET |
            NGX_HTTP_WASM_ABI_CAP_RESP_HEADERS_RW |
            NGX_HTTP_WASM_ABI_CAP_SHARED_KV | NGX_HTTP_WASM_ABI_CAP_METRICS;
    } else if (phase_kind == NGX_HTTP_WASM_PHASE_BALANCER) {
        ctx->abi.capabilities =
            NGX_HTTP_WASM_ABI_CAP_REQ_HEADERS_RO |
            NGX_HTTP_WASM_ABI_CAP_VAR_GET | NGX_HTTP_WASM_ABI_CAP_VAR_SET |
            NGX_HTTP_WASM_ABI_CAP_TIME | NGX_HTTP_WASM_ABI_CAP_SHARED_KV |
            NGX_HTTP_WASM_ABI_CAP_METRICS | NGX_HTTP_WASM_ABI_CAP_BALANCER;
    } else if (phase_kind == NGX_HTTP_WASM_PHASE_BODY_FILTER) {
        ctx->abi.capabilities =
            NGX_HTTP_WASM_ABI_CAP_REQ_HEADERS_RO |
            NGX_HTTP_WASM_ABI_CAP_VAR_GET | NGX_HTTP_WASM_ABI_CAP_TIME |
            NGX_HTTP_WASM_ABI_CAP_RESP_HEADERS_RW |
            NGX_HTTP_WASM_ABI_CAP_RESP_BODY_CHUNK_READ |
            NGX_HTTP_WASM_ABI_CAP_RESP_BODY_CHUNK_WRITE |
            NGX_HTTP_WASM_ABI_CAP_SHARED_KV | NGX_HTTP_WASM_ABI_CAP_METRICS;
    } else if (phase_kind == NGX_HTTP_WASM_PHASE_LOG) {
        ctx->abi.capabilities =
            NGX_HTTP_WASM_ABI_CAP_REQ_HEADERS_RO |
            NGX_HTTP_WASM_ABI_CAP_VAR_GET | NGX_HTTP_WASM_ABI_CAP_TIME |
            NGX_HTTP_WASM_ABI_CAP_RESP_STATUS_GET |
            NGX_HTTP_WASM_ABI_CAP_SHARED_KV | NGX_HTTP_WASM_ABI_CAP_METRICS;
    } else if (phase_kind == NGX_HTTP_WASM_PHASE_SSL_CLIENT_HELLO) {
        ctx->abi.capabilities = NGX_HTTP_WASM_ABI_CAP_SSL_SERVER_NAME_GET |
                                NGX_HTTP_WASM_ABI_CAP_SSL_HANDSHAKE_REJECT |
                                NGX_HTTP_WASM_ABI_CAP_TIME;
    } else if (phase_kind == NGX_HTTP_WASM_PHASE_SSL_CERTIFICATE) {
        ctx->abi.capabilities = NGX_HTTP_WASM_ABI_CAP_SSL_SERVER_NAME_GET |
                                NGX_HTTP_WASM_ABI_CAP_SSL_HANDSHAKE_REJECT |
                                NGX_HTTP_WASM_ABI_CAP_SSL_CERTIFICATE_SET |
                                NGX_HTTP_WASM_ABI_CAP_TIME;
    }
}

#if (NGX_HTTP_SSL)
void ngx_http_wasm_runtime_init_ssl_exec_ctx(
    ngx_http_wasm_exec_ctx_t *ctx,
    ngx_connection_t *c,
    ngx_ssl_conn_t *ssl_conn,
    ngx_http_wasm_phase_conf_t *conf,
    ngx_http_wasm_phase_e phase_kind,
    ngx_http_wasm_runtime_state_t *runtime) {
    ngx_memzero(ctx, sizeof(*ctx));

    ctx->request = NULL;
    ctx->connection = c;
    ctx->ssl_connection = ssl_conn;
    ctx->conf = conf;
    ctx->runtime = runtime;
    ctx->fuel_limit = 0;
    ctx->timeslice_fuel = 0;
    ctx->fuel_remaining = 0;
    ctx->state = NGX_HTTP_WASM_EXEC_READY;
    ctx->suspend_kind = NGX_HTTP_WASM_SUSPEND_NONE;
    ctx->phase_kind = phase_kind;
    ctx->resume_state = NULL;
    ctx->yielded = 0;

    ngx_http_wasm_abi_init(&ctx->abi,
                           NULL,
                           ssl_conn,
                           c,
                           NULL,
                           NULL,
                           NGX_HTTP_WASM_ABI_CAP_SSL_SERVER_NAME_GET |
                               NGX_HTTP_WASM_ABI_CAP_TIME);

    if (phase_kind == NGX_HTTP_WASM_PHASE_SSL_CLIENT_HELLO) {
        ctx->abi.capabilities = NGX_HTTP_WASM_ABI_CAP_SSL_SERVER_NAME_GET |
                                NGX_HTTP_WASM_ABI_CAP_SSL_HANDSHAKE_REJECT |
                                NGX_HTTP_WASM_ABI_CAP_TIME;
    } else if (phase_kind == NGX_HTTP_WASM_PHASE_SSL_CERTIFICATE) {
        ctx->abi.capabilities = NGX_HTTP_WASM_ABI_CAP_SSL_SERVER_NAME_GET |
                                NGX_HTTP_WASM_ABI_CAP_SSL_HANDSHAKE_REJECT |
                                NGX_HTTP_WASM_ABI_CAP_SSL_CERTIFICATE_SET |
                                NGX_HTTP_WASM_ABI_CAP_TIME;
    }
}
#endif

void ngx_http_wasm_runtime_cleanup_exec_ctx(ngx_http_wasm_exec_ctx_t *ctx) {
    if (ctx == NULL || ctx->resume_state == NULL) {
        return;
    }

    ngx_http_wasm_runtime_reset_resume_state(ctx->resume_state);
    ctx->resume_state = NULL;
}

static ngx_log_t *
ngx_http_wasm_runtime_exec_log(ngx_http_wasm_exec_ctx_t *ctx) {
    if (ctx->request != NULL) {
        return ctx->request->connection->log;
    }

    if (ctx->connection != NULL) {
        return ctx->connection->log;
    }

    return ngx_cycle->log;
}

static ngx_pool_t *
ngx_http_wasm_runtime_exec_pool(ngx_http_wasm_exec_ctx_t *ctx) {
    if (ctx->request != NULL) {
        return ctx->request->pool;
    }

    if (ctx->connection != NULL) {
        return ctx->connection->pool;
    }

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_subreq_set_header(void *env,
                                     wasmtime_caller_t *caller,
                                     const wasmtime_val_t *args,
                                     size_t nargs,
                                     wasmtime_val_t *results,
                                     size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *name;
    const u_char *value;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 4 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32 ||
        args[3].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_subreq_set_header signature");
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &name);
    if (trap != NULL) {
        return trap;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[2].of.i32, (uint32_t)args[3].of.i32, &value);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_SUBREQUEST) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_subreq_set_header not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_subrequest_set_header(
        &ctx->abi, name, (size_t)args[1].of.i32, value, (size_t)args[3].of.i32);

    return NULL;
}

static wasm_trap_t *ngx_http_wasm_host_subreq(void *env,
                                              wasmtime_caller_t *caller,
                                              const wasmtime_val_t *args,
                                              size_t nargs,
                                              wasmtime_val_t *results,
                                              size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *uri;
    const u_char *query;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 6 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32 ||
        args[3].kind != WASMTIME_I32 || args[4].kind != WASMTIME_I32 ||
        args[5].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_subreq signature");
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &uri);
    if (trap != NULL) {
        return trap;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[2].of.i32, (uint32_t)args[3].of.i32, &query);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_SUBREQUEST) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_subreq not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 =
        ngx_http_wasm_abi_subrequest(&ctx->abi,
                                     uri,
                                     (size_t)args[1].of.i32,
                                     query,
                                     (size_t)args[3].of.i32,
                                     args[4].of.i32,
                                     (ngx_uint_t)args[5].of.i32);

    if (results[0].of.i32 == NGX_HTTP_WASM_OK) {
        ctx->yielded = 1;
        ctx->suspend_kind = NGX_HTTP_WASM_SUSPEND_WAIT_IO;
    }

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_subreq_get_status(void *env,
                                     wasmtime_caller_t *caller,
                                     const wasmtime_val_t *args,
                                     size_t nargs,
                                     wasmtime_val_t *results,
                                     size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;

    (void)env;

    if (nargs != 0 || nresults != 1) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_subreq_get_status signature");
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_SUBREQUEST) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_subreq_get_status not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_subrequest_get_status(&ctx->abi);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_subreq_get_header(void *env,
                                     wasmtime_caller_t *caller,
                                     const wasmtime_val_t *args,
                                     size_t nargs,
                                     wasmtime_val_t *results,
                                     size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *name;
    u_char *buf;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 4 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32 ||
        args[3].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_subreq_get_header signature");
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &name);
    if (trap != NULL) {
        return trap;
    }

    trap = ngx_http_wasm_runtime_get_memory_mut(
        caller, (uint32_t)args[2].of.i32, (uint32_t)args[3].of.i32, &buf);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_SUBREQUEST) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_subreq_get_header not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_subrequest_get_header(
        &ctx->abi, name, (size_t)args[1].of.i32, buf, (size_t)args[3].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_subreq_get_body(void *env,
                                   wasmtime_caller_t *caller,
                                   const wasmtime_val_t *args,
                                   size_t nargs,
                                   wasmtime_val_t *results,
                                   size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    u_char *buf;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 2 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_subreq_get_body signature");
    }

    trap = ngx_http_wasm_runtime_get_memory_mut(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &buf);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_SUBREQUEST) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_subreq_get_body not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_subrequest_get_body(
        &ctx->abi, buf, (size_t)args[1].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_subreq_get_body_len(void *env,
                                       wasmtime_caller_t *caller,
                                       const wasmtime_val_t *args,
                                       size_t nargs,
                                       wasmtime_val_t *results,
                                       size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;

    (void)env;

    if (nargs != 0 || nresults != 1) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_subreq_get_body_len signature");
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_SUBREQUEST) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_subreq_get_body_len not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_subrequest_get_body_len(&ctx->abi);

    return NULL;
}

static ngx_int_t
ngx_http_wasm_runtime_define_host_funcs(ngx_http_wasm_runtime_state_t *rt) {
    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_log",
            wasm_functype_new_3_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_log) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_resp_set_status",
            wasm_functype_new_1_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_resp_set_status) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_resp_get_status",
            wasm_functype_new_0_1(wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_resp_get_status) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_metric_counter_inc",
            wasm_functype_new_3_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_metric_counter_inc) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_metric_gauge_set",
            wasm_functype_new_3_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_metric_gauge_set) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_metric_gauge_add",
            wasm_functype_new_3_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_metric_gauge_add) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_balancer_set_peer",
            wasm_functype_new_1_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_balancer_set_peer) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(rt,
                                          "ngx_wasm_shm_get",
                                          ngx_http_wasm_runtime_functype_4_1(),
                                          ngx_http_wasm_host_shm_get) !=
        NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(rt,
                                          "ngx_wasm_shm_set",
                                          ngx_http_wasm_runtime_functype_4_1(),
                                          ngx_http_wasm_host_shm_set) !=
        NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_shm_delete",
            wasm_functype_new_2_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_shm_delete) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_ssl_get_server_name",
            wasm_functype_new_2_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_ssl_get_server_name) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_ssl_reject_handshake",
            wasm_functype_new_1_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_ssl_reject_handshake) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_ssl_set_certificate",
            ngx_http_wasm_runtime_functype_4_1(),
            ngx_http_wasm_host_ssl_set_certificate) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(rt,
                                          "ngx_wasm_req_set_header",
                                          ngx_http_wasm_runtime_functype_4_1(),
                                          ngx_http_wasm_host_req_set_header) !=
        NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(rt,
                                          "ngx_wasm_req_get_header",
                                          ngx_http_wasm_runtime_functype_4_1(),
                                          ngx_http_wasm_host_req_get_header) !=
        NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(rt,
                                          "ngx_wasm_var_get",
                                          ngx_http_wasm_runtime_functype_4_1(),
                                          ngx_http_wasm_host_var_get) !=
        NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(rt,
                                          "ngx_wasm_var_set",
                                          ngx_http_wasm_runtime_functype_4_1(),
                                          ngx_http_wasm_host_var_set) !=
        NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_time_unix_ms",
            wasm_functype_new_2_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_time_unix_ms) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_time_monotonic_ms",
            wasm_functype_new_2_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_time_monotonic_ms) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_subreq_set_header",
            ngx_http_wasm_runtime_functype_4_1(),
            ngx_http_wasm_host_subreq_set_header) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(rt,
                                          "ngx_wasm_subreq",
                                          ngx_http_wasm_runtime_functype_6_1(),
                                          ngx_http_wasm_host_subreq) !=
        NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_subreq_get_status",
            wasm_functype_new_0_1(wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_subreq_get_status) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_subreq_get_header",
            ngx_http_wasm_runtime_functype_4_1(),
            ngx_http_wasm_host_subreq_get_header) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_subreq_get_body",
            wasm_functype_new_2_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_subreq_get_body) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_subreq_get_body_len",
            wasm_functype_new_0_1(wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_subreq_get_body_len) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(rt,
                                          "ngx_wasm_resp_set_header",
                                          ngx_http_wasm_runtime_functype_4_1(),
                                          ngx_http_wasm_host_resp_set_header) !=
        NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(rt,
                                          "ngx_wasm_resp_get_header",
                                          ngx_http_wasm_runtime_functype_4_1(),
                                          ngx_http_wasm_host_resp_get_header) !=
        NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_resp_get_body_chunk",
            wasm_functype_new_2_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_resp_get_body_chunk) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_resp_get_body_chunk_eof",
            wasm_functype_new_0_1(wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_resp_get_body_chunk_eof) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_resp_set_body_chunk",
            wasm_functype_new_2_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_resp_set_body_chunk) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_req_get_body",
            wasm_functype_new_2_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_req_get_body) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_resp_write",
            wasm_functype_new_2_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_resp_write) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_func(
            rt,
            "ngx_wasm_yield",
            wasm_functype_new_0_1(wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_yield) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

static ngx_int_t
ngx_http_wasm_runtime_define_func(ngx_http_wasm_runtime_state_t *rt,
                                  const char *name,
                                  wasm_functype_t *ty,
                                  wasmtime_func_callback_t cb) {
    wasmtime_error_t *error;

    error = wasmtime_linker_define_func(rt->linker,
                                        NGX_HTTP_WASM_IMPORT_MODULE,
                                        sizeof(NGX_HTTP_WASM_IMPORT_MODULE) - 1,
                                        name,
                                        ngx_strlen(name),
                                        ty,
                                        cb,
                                        NULL,
                                        NULL);

    wasm_functype_delete(ty);

    if (error != NULL) {
        ngx_http_wasm_runtime_log_error(rt->log, name, error);
        return NGX_ERROR;
    }

    return NGX_OK;
}

static wasm_functype_t *ngx_http_wasm_runtime_functype_4_1(void) {
    wasm_valtype_t *params[4];
    wasm_valtype_t *results[1];
    wasm_valtype_vec_t params_vec;
    wasm_valtype_vec_t results_vec;

    params[0] = wasm_valtype_new(WASM_I32);
    params[1] = wasm_valtype_new(WASM_I32);
    params[2] = wasm_valtype_new(WASM_I32);
    params[3] = wasm_valtype_new(WASM_I32);
    results[0] = wasm_valtype_new(WASM_I32);

    wasm_valtype_vec_new(&params_vec, 4, params);
    wasm_valtype_vec_new(&results_vec, 1, results);

    return wasm_functype_new(&params_vec, &results_vec);
}

static wasm_functype_t *ngx_http_wasm_runtime_functype_6_1(void) {
    wasm_valtype_t *params[6];
    wasm_valtype_t *results[1];
    wasm_valtype_vec_t params_vec;
    wasm_valtype_vec_t results_vec;

    params[0] = wasm_valtype_new(WASM_I32);
    params[1] = wasm_valtype_new(WASM_I32);
    params[2] = wasm_valtype_new(WASM_I32);
    params[3] = wasm_valtype_new(WASM_I32);
    params[4] = wasm_valtype_new(WASM_I32);
    params[5] = wasm_valtype_new(WASM_I32);
    results[0] = wasm_valtype_new(WASM_I32);

    wasm_valtype_vec_new(&params_vec, 6, params);
    wasm_valtype_vec_new(&results_vec, 1, results);

    return wasm_functype_new(&params_vec, &results_vec);
}

static ngx_http_wasm_cached_module_t *
ngx_http_wasm_runtime_find_module(ngx_http_wasm_main_conf_t *wmcf,
                                  const ngx_str_t *path) {
    ngx_http_wasm_cached_module_t **modules;
    ngx_uint_t i;

    if (wmcf->modules == NULL) {
        return NULL;
    }

    modules = wmcf->modules->elts;
    for (i = 0; i < wmcf->modules->nelts; i++) {
        if (modules[i]->module_path.len == path->len &&
            ngx_strncmp(modules[i]->module_path.data, path->data, path->len) ==
                0) {
            return modules[i];
        }
    }

    return NULL;
}

ngx_http_wasm_cached_module_t *ngx_http_wasm_runtime_get_or_load(
    ngx_conf_t *cf, ngx_http_wasm_main_conf_t *wmcf, ngx_str_t *path) {
    ngx_http_wasm_cached_module_t *entry;
    ngx_http_wasm_cached_module_t **slot;
    ngx_str_t bytes;
    wasmtime_error_t *error;
    u_char *module_path;

    entry = ngx_http_wasm_runtime_find_module(wmcf, path);
    if (entry != NULL) {
        return entry;
    }

    entry = ngx_pcalloc(cf->pool, sizeof(*entry));
    if (entry == NULL) {
        return NULL;
    }

    module_path = ngx_pnalloc(cf->pool, path->len + 1);
    if (module_path == NULL) {
        return NULL;
    }

    ngx_memcpy(module_path, path->data, path->len);
    module_path[path->len] = '\0';
    entry->module_path.data = module_path;
    entry->module_path.len = path->len;

    if (ngx_http_wasm_runtime_read_file(
            cf->pool, cf->log, entry->module_path.data, &bytes) != NGX_OK) {
        return NULL;
    }

    error = ngx_http_wasm_runtime_compile_module(
        wmcf->runtime, &bytes, &entry->module);
    if (error != NULL) {
        ngx_http_wasm_runtime_log_error(
            cf->log, "failed to compile guest module", error);
        return NULL;
    }

    slot = ngx_array_push(wmcf->modules);
    if (slot == NULL) {
        wasmtime_module_delete(entry->module);
        entry->module = NULL;
        return NULL;
    }

    *slot = entry;
    return entry;
}

static ngx_int_t ngx_http_wasm_runtime_read_file(ngx_pool_t *pool,
                                                 ngx_log_t *log,
                                                 const u_char *path,
                                                 ngx_str_t *out) {
    ngx_fd_t fd;
    ngx_file_info_t fi;
    ngx_file_t file;
    off_t size;
    ssize_t n;
    u_char *data;

    fd = ngx_open_file((u_char *)path, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR,
                      log,
                      ngx_errno,
                      "ngx_wasm: failed to open module \"%s\"",
                      path);
        return NGX_ERROR;
    }

    if (ngx_fd_info(fd, &fi) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ERR,
                      log,
                      ngx_errno,
                      "ngx_wasm: failed to stat module \"%s\"",
                      path);
        ngx_close_file(fd);
        return NGX_ERROR;
    }

    size = ngx_file_size(&fi);
    if (size < 0 || (uint64_t)size > NGX_MAX_SIZE_T_VALUE) {
        ngx_log_error(NGX_LOG_ERR,
                      log,
                      0,
                      "ngx_wasm: invalid module size for \"%s\"",
                      path);
        ngx_close_file(fd);
        return NGX_ERROR;
    }

    data = ngx_pnalloc(pool, (size_t)size);
    if (data == NULL) {
        ngx_close_file(fd);
        return NGX_ERROR;
    }

    ngx_memzero(&file, sizeof(file));
    file.fd = fd;
    file.name.data = (u_char *)path;
    file.name.len = ngx_strlen(path);
    file.log = log;

    n = ngx_read_file(&file, data, (size_t)size, 0);
    ngx_close_file(fd);

    if (n == NGX_ERROR || (size_t)n != (size_t)size) {
        ngx_log_error(NGX_LOG_ERR,
                      log,
                      ngx_errno,
                      "ngx_wasm: failed to read module \"%s\"",
                      path);
        return NGX_ERROR;
    }

    out->data = data;
    out->len = (size_t)size;

    return NGX_OK;
}

static wasmtime_error_t *
ngx_http_wasm_runtime_compile_module(ngx_http_wasm_runtime_state_t *rt,
                                     const ngx_str_t *bytes,
                                     wasmtime_module_t **module) {
    static u_char wasm_magic[] = {0x00, 0x61, 0x73, 0x6d};
    wasm_byte_vec_t binary;
    wasmtime_error_t *error;

    if (bytes->len >= sizeof(wasm_magic) &&
        ngx_memcmp(bytes->data, wasm_magic, sizeof(wasm_magic)) == 0) {
        return wasmtime_module_new(rt->engine, bytes->data, bytes->len, module);
    }

    error = wasmtime_wat2wasm((const char *)bytes->data, bytes->len, &binary);
    if (error != NULL) {
        return error;
    }

    error = wasmtime_module_new(
        rt->engine, (uint8_t *)binary.data, binary.size, module);
    wasm_byte_vec_delete(&binary);

    return error;
}

static void ngx_http_wasm_runtime_log_error(ngx_log_t *log,
                                            const char *context,
                                            wasmtime_error_t *error) {
    wasm_name_t message;

    wasmtime_error_message(error, &message);
    ngx_log_error(NGX_LOG_ERR,
                  log,
                  0,
                  "ngx_wasm: %s: %*s",
                  context,
                  (int)message.size,
                  message.data);
    wasm_byte_vec_delete(&message);
    wasmtime_error_delete(error);
}

static void ngx_http_wasm_runtime_log_trap(ngx_log_t *log,
                                           const char *context,
                                           wasm_trap_t *trap) {
    wasm_name_t message;
    wasmtime_trap_code_t code;

    wasm_trap_message(trap, &message);

    if (wasmtime_trap_code(trap, &code)) {
        ngx_log_error(NGX_LOG_ERR,
                      log,
                      0,
                      "ngx_wasm: %s: trap=%d message=%*s",
                      context,
                      (int)code,
                      (int)message.size,
                      message.data);
    } else {
        ngx_log_error(NGX_LOG_ERR,
                      log,
                      0,
                      "ngx_wasm: %s: %*s",
                      context,
                      (int)message.size,
                      message.data);
    }

    wasm_byte_vec_delete(&message);
    wasm_trap_delete(trap);
}

static ngx_int_t
ngx_http_wasm_runtime_update_fuel(ngx_http_wasm_exec_ctx_t *ctx,
                                  wasmtime_context_t *context,
                                  const char *phase) {
    uint64_t fuel;
    wasmtime_error_t *error;

    error = wasmtime_context_get_fuel(context, &fuel);
    if (error != NULL) {
        ngx_http_wasm_runtime_log_error(
            ngx_http_wasm_runtime_exec_log(ctx), phase, error);
        return NGX_ERROR;
    }

    ctx->fuel_remaining = fuel;

    return NGX_OK;
}

static void ngx_http_wasm_runtime_begin_run(ngx_http_wasm_exec_ctx_t *ctx) {
    ctx->state = NGX_HTTP_WASM_EXEC_RUNNING;
    ctx->suspend_kind = NGX_HTTP_WASM_SUSPEND_NONE;
    ctx->yielded = 0;
}

static void ngx_http_wasm_runtime_log_suspend(ngx_http_wasm_exec_ctx_t *ctx,
                                              const char *reason) {
    ngx_log_error(NGX_LOG_NOTICE,
                  ngx_http_wasm_runtime_exec_log(ctx),
                  0,
                  "ngx_wasm: suspending request: reason=%s fuel_limit=%uL "
                  "timeslice_fuel=%uL fuel_remaining=%uL",
                  reason,
                  (unsigned long long)ctx->fuel_limit,
                  (unsigned long long)ctx->timeslice_fuel,
                  (unsigned long long)ctx->fuel_remaining);
}

static ngx_int_t
ngx_http_wasm_runtime_suspend(ngx_http_wasm_exec_ctx_t *ctx,
                              ngx_http_wasm_suspend_kind_e kind,
                              const char *reason) {
    ctx->state = NGX_HTTP_WASM_EXEC_SUSPENDED;
    ctx->suspend_kind = kind;
    ngx_http_wasm_runtime_log_suspend(ctx, reason);

    return NGX_AGAIN;
}

static void
ngx_http_wasm_runtime_clear_async_future(ngx_http_wasm_resume_state_t *resume) {
    if (resume->future != NULL) {
        wasmtime_call_future_delete(resume->future);
        resume->future = NULL;
    }
    resume->future_kind = NGX_HTTP_WASM_FUTURE_NONE;
}

static void ngx_http_wasm_runtime_clear_async_outputs(
    ngx_http_wasm_resume_state_t *resume) {
    if (resume->trap != NULL) {
        wasm_trap_delete(resume->trap);
        resume->trap = NULL;
    }

    if (resume->error != NULL) {
        wasmtime_error_delete(resume->error);
        resume->error = NULL;
    }

    resume->results[0].kind = WASMTIME_I32;
    resume->results[0].of.i32 = 0;
}

static void
ngx_http_wasm_runtime_reset_resume_state(ngx_http_wasm_resume_state_t *resume) {
    ngx_http_wasm_runtime_clear_async_future(resume);
    ngx_http_wasm_runtime_clear_async_outputs(resume);

    if (resume->store != NULL) {
        wasmtime_store_delete(resume->store);
        resume->store = NULL;
    }

    ngx_memzero(&resume->instance, sizeof(resume->instance));
    ngx_memzero(&resume->func, sizeof(resume->func));
    resume->instance_ready = 0;
    resume->func_ready = 0;
}

static ngx_int_t
ngx_http_wasm_runtime_prepare_async_op(ngx_http_wasm_exec_ctx_t *ctx,
                                       ngx_http_wasm_resume_state_t *resume,
                                       ngx_uint_t future_kind) {
    if ((resume->future == NULL) !=
        (resume->future_kind == NGX_HTTP_WASM_FUTURE_NONE)) {
        ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
        ngx_log_error(NGX_LOG_ERR,
                      ngx_http_wasm_runtime_exec_log(ctx),
                      0,
                      "ngx_wasm: inconsistent async future state");
        return NGX_ERROR;
    }

    if (resume->future != NULL) {
        ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
        ngx_log_error(NGX_LOG_ERR,
                      ngx_http_wasm_runtime_exec_log(ctx),
                      0,
                      "ngx_wasm: attempted to start async work while another "
                      "future is still alive");
        return NGX_ERROR;
    }

    ngx_http_wasm_runtime_clear_async_outputs(resume);
    resume->future_kind = future_kind;

    return NGX_OK;
}

static ngx_int_t
ngx_http_wasm_runtime_prepare_async_yield(ngx_http_wasm_exec_ctx_t *ctx,
                                          wasmtime_context_t *context) {
    uint64_t interval;
    wasmtime_error_t *error;

    if (ctx->fuel_remaining == 0) {
        return NGX_ERROR;
    }

    interval = ctx->fuel_remaining;
    if (ctx->timeslice_fuel > 0 && ctx->timeslice_fuel < interval) {
        interval = ctx->timeslice_fuel;
    }

    error = wasmtime_context_fuel_async_yield_interval(context, interval);
    if (error != NULL) {
        ngx_http_wasm_runtime_log_error(
            ngx_http_wasm_runtime_exec_log(ctx),
            "failed to configure async fuel yield interval",
            error);
        return NGX_ERROR;
    }

    return NGX_OK;
}

static wasm_trap_t *ngx_http_wasm_runtime_bad_signature(const char *name) {
    return wasmtime_trap_new(name, ngx_strlen(name));
}

static wasm_trap_t *ngx_http_wasm_runtime_phase_forbidden(const char *name) {
    return wasmtime_trap_new(name, ngx_strlen(name));
}

static wasm_trap_t *ngx_http_wasm_runtime_get_memory(wasmtime_caller_t *caller,
                                                     uint32_t ptr,
                                                     uint32_t len,
                                                     const u_char **data) {
    wasmtime_extern_t item;
    wasmtime_context_t *context;
    uint8_t *base;
    size_t size;
    uint64_t end;

    if (!wasmtime_caller_export_get(caller,
                                    NGX_HTTP_WASM_MEMORY_EXPORT,
                                    sizeof(NGX_HTTP_WASM_MEMORY_EXPORT) - 1,
                                    &item) ||
        item.kind != WASMTIME_EXTERN_MEMORY) {
        return wasmtime_trap_new("guest memory export not found",
                                 sizeof("guest memory export not found") - 1);
    }

    context = wasmtime_caller_context(caller);
    base = wasmtime_memory_data(context, &item.of.memory);
    size = wasmtime_memory_data_size(context, &item.of.memory);

    end = (uint64_t)ptr + (uint64_t)len;
    if (end > size) {
        return wasmtime_trap_new("guest memory access out of bounds",
                                 sizeof("guest memory access out of bounds") -
                                     1);
    }

    *data = base + ptr;
    return NULL;
}

static wasm_trap_t *ngx_http_wasm_runtime_get_memory_mut(
    wasmtime_caller_t *caller, uint32_t ptr, uint32_t len, u_char **data) {
    const u_char *ro_data;
    wasm_trap_t *trap;

    trap = ngx_http_wasm_runtime_get_memory(caller, ptr, len, &ro_data);
    if (trap != NULL) {
        return trap;
    }

    *data = (u_char *)ro_data;

    return NULL;
}

static wasm_trap_t *ngx_http_wasm_host_log(void *env,
                                           wasmtime_caller_t *caller,
                                           const wasmtime_val_t *args,
                                           size_t nargs,
                                           wasmtime_val_t *results,
                                           size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *data;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 3 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_log signature");
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[1].of.i32, (uint32_t)args[2].of.i32, &data);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_log(
        &ctx->abi, (ngx_uint_t)args[0].of.i32, data, (size_t)args[2].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_resp_set_status(void *env,
                                   wasmtime_caller_t *caller,
                                   const wasmtime_val_t *args,
                                   size_t nargs,
                                   wasmtime_val_t *results,
                                   size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;

    (void)env;

    if (nargs != 1 || nresults != 1 || args[0].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_resp_set_status signature");
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_RESP_STATUS_SET) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_resp_set_status not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 =
        ngx_http_wasm_abi_resp_set_status(&ctx->abi, args[0].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_req_set_header(void *env,
                                  wasmtime_caller_t *caller,
                                  const wasmtime_val_t *args,
                                  size_t nargs,
                                  wasmtime_val_t *results,
                                  size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *name;
    const u_char *value;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 4 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32 ||
        args[3].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_req_set_header signature");
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &name);
    if (trap != NULL) {
        return trap;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[2].of.i32, (uint32_t)args[3].of.i32, &value);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_REQ_HEADERS_RW) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_req_set_header not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_req_set_header(
        &ctx->abi, name, (size_t)args[1].of.i32, value, (size_t)args[3].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_metric_counter_inc(void *env,
                                      wasmtime_caller_t *caller,
                                      const wasmtime_val_t *args,
                                      size_t nargs,
                                      wasmtime_val_t *results,
                                      size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *name;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 3 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_metric_counter_inc signature");
    }

    if (args[0].of.i32 < 0 || args[1].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &name);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_METRICS) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_metric_counter_inc not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_metric_counter_inc(
        &ctx->abi, name, (size_t)args[1].of.i32, args[2].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_metric_gauge_set(void *env,
                                    wasmtime_caller_t *caller,
                                    const wasmtime_val_t *args,
                                    size_t nargs,
                                    wasmtime_val_t *results,
                                    size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *name;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 3 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_metric_gauge_set signature");
    }

    if (args[0].of.i32 < 0 || args[1].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &name);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_METRICS) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_metric_gauge_set not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_metric_gauge_set(
        &ctx->abi, name, (size_t)args[1].of.i32, args[2].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_metric_gauge_add(void *env,
                                    wasmtime_caller_t *caller,
                                    const wasmtime_val_t *args,
                                    size_t nargs,
                                    wasmtime_val_t *results,
                                    size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *name;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 3 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_metric_gauge_add signature");
    }

    if (args[0].of.i32 < 0 || args[1].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &name);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_METRICS) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_metric_gauge_add not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_metric_gauge_add(
        &ctx->abi, name, (size_t)args[1].of.i32, args[2].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_balancer_set_peer(void *env,
                                     wasmtime_caller_t *caller,
                                     const wasmtime_val_t *args,
                                     size_t nargs,
                                     wasmtime_val_t *results,
                                     size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;

    (void)env;

    if (nargs != 1 || nresults != 1 || args[0].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_balancer_set_peer signature");
    }

    if (args[0].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_BALANCER) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_balancer_set_peer not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 =
        ngx_http_wasm_abi_balancer_set_peer(&ctx->abi, args[0].of.i32);

    return NULL;
}

static wasm_trap_t *ngx_http_wasm_host_shm_get(void *env,
                                               wasmtime_caller_t *caller,
                                               const wasmtime_val_t *args,
                                               size_t nargs,
                                               wasmtime_val_t *results,
                                               size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *key;
    u_char *buf;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 4 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32 ||
        args[3].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_shm_get signature");
    }

    if (args[0].of.i32 < 0 || args[1].of.i32 < 0 || args[2].of.i32 < 0 ||
        args[3].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &key);
    if (trap != NULL) {
        return trap;
    }

    trap = ngx_http_wasm_runtime_get_memory_mut(
        caller, (uint32_t)args[2].of.i32, (uint32_t)args[3].of.i32, &buf);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_SHARED_KV) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_shm_get not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_shm_get(
        &ctx->abi, key, (size_t)args[1].of.i32, buf, (size_t)args[3].of.i32);

    return NULL;
}

static wasm_trap_t *ngx_http_wasm_host_shm_set(void *env,
                                               wasmtime_caller_t *caller,
                                               const wasmtime_val_t *args,
                                               size_t nargs,
                                               wasmtime_val_t *results,
                                               size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *key;
    const u_char *value;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 4 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32 ||
        args[3].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_shm_set signature");
    }

    if (args[0].of.i32 < 0 || args[1].of.i32 < 0 || args[2].of.i32 < 0 ||
        args[3].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &key);
    if (trap != NULL) {
        return trap;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[2].of.i32, (uint32_t)args[3].of.i32, &value);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_SHARED_KV) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_shm_set not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_shm_set(
        &ctx->abi, key, (size_t)args[1].of.i32, value, (size_t)args[3].of.i32);

    return NULL;
}

static wasm_trap_t *ngx_http_wasm_host_shm_delete(void *env,
                                                  wasmtime_caller_t *caller,
                                                  const wasmtime_val_t *args,
                                                  size_t nargs,
                                                  wasmtime_val_t *results,
                                                  size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *key;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 2 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_shm_delete signature");
    }

    if (args[0].of.i32 < 0 || args[1].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &key);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_SHARED_KV) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_shm_delete not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 =
        ngx_http_wasm_abi_shm_delete(&ctx->abi, key, (size_t)args[1].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_resp_get_status(void *env,
                                   wasmtime_caller_t *caller,
                                   const wasmtime_val_t *args,
                                   size_t nargs,
                                   wasmtime_val_t *results,
                                   size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;

    (void)env;

    if (nargs != 0 || nresults != 1) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_resp_get_status signature");
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_RESP_STATUS_GET) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_resp_get_status not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_resp_get_status(&ctx->abi);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_ssl_get_server_name(void *env,
                                       wasmtime_caller_t *caller,
                                       const wasmtime_val_t *args,
                                       size_t nargs,
                                       wasmtime_val_t *results,
                                       size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    u_char *buf;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 2 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_ssl_get_server_name signature");
    }

    trap = ngx_http_wasm_runtime_get_memory_mut(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &buf);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_ssl_get_server_name(
        &ctx->abi, buf, (size_t)args[1].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_ssl_reject_handshake(void *env,
                                        wasmtime_caller_t *caller,
                                        const wasmtime_val_t *args,
                                        size_t nargs,
                                        wasmtime_val_t *results,
                                        size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;

    (void)env;
    (void)caller;

    if (nargs != 1 || nresults != 1 || args[0].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_ssl_reject_handshake signature");
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 =
        ngx_http_wasm_abi_ssl_reject_handshake(&ctx->abi, args[0].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_ssl_set_certificate(void *env,
                                       wasmtime_caller_t *caller,
                                       const wasmtime_val_t *args,
                                       size_t nargs,
                                       wasmtime_val_t *results,
                                       size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *cert;
    const u_char *key;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 4 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32 ||
        args[3].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_ssl_set_certificate signature");
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &cert);
    if (trap != NULL) {
        return trap;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[2].of.i32, (uint32_t)args[3].of.i32, &key);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_ssl_set_certificate(
        &ctx->abi, cert, (size_t)args[1].of.i32, key, (size_t)args[3].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_req_get_header(void *env,
                                  wasmtime_caller_t *caller,
                                  const wasmtime_val_t *args,
                                  size_t nargs,
                                  wasmtime_val_t *results,
                                  size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *name;
    u_char *buf;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 4 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32 ||
        args[3].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_req_get_header signature");
    }

    if (args[0].of.i32 < 0 || args[1].of.i32 < 0 || args[2].of.i32 < 0 ||
        args[3].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &name);
    if (trap != NULL) {
        return trap;
    }

    trap = ngx_http_wasm_runtime_get_memory_mut(
        caller, (uint32_t)args[2].of.i32, (uint32_t)args[3].of.i32, &buf);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_req_get_header(
        &ctx->abi, name, (size_t)args[1].of.i32, buf, (size_t)args[3].of.i32);

    return NULL;
}

static wasm_trap_t *ngx_http_wasm_host_var_get(void *env,
                                               wasmtime_caller_t *caller,
                                               const wasmtime_val_t *args,
                                               size_t nargs,
                                               wasmtime_val_t *results,
                                               size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *name;
    u_char *buf;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 4 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32 ||
        args[3].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_var_get signature");
    }

    if (args[0].of.i32 < 0 || args[1].of.i32 < 0 || args[2].of.i32 < 0 ||
        args[3].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &name);
    if (trap != NULL) {
        return trap;
    }

    trap = ngx_http_wasm_runtime_get_memory_mut(
        caller, (uint32_t)args[2].of.i32, (uint32_t)args[3].of.i32, &buf);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_VAR_GET) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_var_get not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_var_get(
        &ctx->abi, name, (size_t)args[1].of.i32, buf, (size_t)args[3].of.i32);

    return NULL;
}

static wasm_trap_t *ngx_http_wasm_host_var_set(void *env,
                                               wasmtime_caller_t *caller,
                                               const wasmtime_val_t *args,
                                               size_t nargs,
                                               wasmtime_val_t *results,
                                               size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *name;
    const u_char *value;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 4 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32 ||
        args[3].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_var_set signature");
    }

    if (args[0].of.i32 < 0 || args[1].of.i32 < 0 || args[2].of.i32 < 0 ||
        args[3].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &name);
    if (trap != NULL) {
        return trap;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[2].of.i32, (uint32_t)args[3].of.i32, &value);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_VAR_SET) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_var_set not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_var_set(
        &ctx->abi, name, (size_t)args[1].of.i32, value, (size_t)args[3].of.i32);

    return NULL;
}

static wasm_trap_t *ngx_http_wasm_host_time_unix_ms(void *env,
                                                    wasmtime_caller_t *caller,
                                                    const wasmtime_val_t *args,
                                                    size_t nargs,
                                                    wasmtime_val_t *results,
                                                    size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    u_char *buf;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 2 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_time_unix_ms signature");
    }

    if (args[0].of.i32 < 0 || args[1].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    trap = ngx_http_wasm_runtime_get_memory_mut(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &buf);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_TIME) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_time_unix_ms not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 =
        ngx_http_wasm_abi_time_unix_ms(&ctx->abi, buf, (size_t)args[1].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_time_monotonic_ms(void *env,
                                     wasmtime_caller_t *caller,
                                     const wasmtime_val_t *args,
                                     size_t nargs,
                                     wasmtime_val_t *results,
                                     size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    u_char *buf;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 2 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_time_monotonic_ms signature");
    }

    if (args[0].of.i32 < 0 || args[1].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    trap = ngx_http_wasm_runtime_get_memory_mut(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &buf);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_TIME) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_time_monotonic_ms not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_time_monotonic_ms(
        &ctx->abi, buf, (size_t)args[1].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_resp_set_header(void *env,
                                   wasmtime_caller_t *caller,
                                   const wasmtime_val_t *args,
                                   size_t nargs,
                                   wasmtime_val_t *results,
                                   size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *name;
    const u_char *value;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 4 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32 ||
        args[3].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_resp_set_header signature");
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &name);
    if (trap != NULL) {
        return trap;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[2].of.i32, (uint32_t)args[3].of.i32, &value);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_RESP_HEADERS_RW) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_resp_set_header not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_resp_set_header(
        &ctx->abi, name, (size_t)args[1].of.i32, value, (size_t)args[3].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_resp_get_header(void *env,
                                   wasmtime_caller_t *caller,
                                   const wasmtime_val_t *args,
                                   size_t nargs,
                                   wasmtime_val_t *results,
                                   size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *name;
    u_char *buf;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 4 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32 || args[2].kind != WASMTIME_I32 ||
        args[3].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_resp_get_header signature");
    }

    if (args[2].of.i32 < 0 || args[3].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &name);
    if (trap != NULL) {
        return trap;
    }

    trap = ngx_http_wasm_runtime_get_memory_mut(
        caller, (uint32_t)args[2].of.i32, (uint32_t)args[3].of.i32, &buf);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_RESP_HEADERS_RW) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_resp_get_header not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_resp_get_header(
        &ctx->abi, name, (size_t)args[1].of.i32, buf, (size_t)args[3].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_resp_get_body_chunk(void *env,
                                       wasmtime_caller_t *caller,
                                       const wasmtime_val_t *args,
                                       size_t nargs,
                                       wasmtime_val_t *results,
                                       size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    u_char *buf;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 2 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_resp_get_body_chunk signature");
    }

    if (args[0].of.i32 < 0 || args[1].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    trap = ngx_http_wasm_runtime_get_memory_mut(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &buf);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_RESP_BODY_CHUNK_READ) ==
        0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_resp_get_body_chunk not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_resp_get_body_chunk(
        &ctx->abi, buf, (size_t)args[1].of.i32);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_resp_get_body_chunk_eof(void *env,
                                           wasmtime_caller_t *caller,
                                           const wasmtime_val_t *args,
                                           size_t nargs,
                                           wasmtime_val_t *results,
                                           size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;

    (void)env;

    if (nargs != 0 || nresults != 1) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_resp_get_body_chunk_eof signature");
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_RESP_BODY_CHUNK_READ) ==
        0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_resp_get_body_chunk_eof not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_resp_get_body_chunk_eof(&ctx->abi);

    return NULL;
}

static wasm_trap_t *
ngx_http_wasm_host_resp_set_body_chunk(void *env,
                                       wasmtime_caller_t *caller,
                                       const wasmtime_val_t *args,
                                       size_t nargs,
                                       wasmtime_val_t *results,
                                       size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *data;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 2 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_resp_set_body_chunk signature");
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &data);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_RESP_BODY_CHUNK_WRITE) ==
        0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_resp_set_body_chunk not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_resp_set_body_chunk(
        &ctx->abi, data, (size_t)args[1].of.i32);

    return NULL;
}

static wasm_trap_t *ngx_http_wasm_host_req_get_body(void *env,
                                                    wasmtime_caller_t *caller,
                                                    const wasmtime_val_t *args,
                                                    size_t nargs,
                                                    wasmtime_val_t *results,
                                                    size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    u_char *buf;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 2 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_req_get_body signature");
    }

    if (args[0].of.i32 < 0 || args[1].of.i32 < 0) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = NGX_HTTP_WASM_ERROR;
        return NULL;
    }

    trap = ngx_http_wasm_runtime_get_memory_mut(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &buf);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_REQ_BODY_GET) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_req_get_body not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 =
        ngx_http_wasm_abi_req_get_body(&ctx->abi, buf, (size_t)args[1].of.i32);

    return NULL;
}

static wasm_trap_t *ngx_http_wasm_host_resp_write(void *env,
                                                  wasmtime_caller_t *caller,
                                                  const wasmtime_val_t *args,
                                                  size_t nargs,
                                                  wasmtime_val_t *results,
                                                  size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;
    const u_char *data;
    wasm_trap_t *trap;

    (void)env;

    if (nargs != 2 || nresults != 1 || args[0].kind != WASMTIME_I32 ||
        args[1].kind != WASMTIME_I32) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_resp_write signature");
    }

    trap = ngx_http_wasm_runtime_get_memory(
        caller, (uint32_t)args[0].of.i32, (uint32_t)args[1].of.i32, &data);
    if (trap != NULL) {
        return trap;
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_RESP_BODY_WRITE) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_resp_write not allowed in this phase");
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_resp_write(
        &ctx->abi, data, (size_t)args[1].of.i32, 1);

    return NULL;
}

static wasm_trap_t *ngx_http_wasm_host_yield(void *env,
                                             wasmtime_caller_t *caller,
                                             const wasmtime_val_t *args,
                                             size_t nargs,
                                             wasmtime_val_t *results,
                                             size_t nresults) {
    ngx_http_wasm_exec_ctx_t *ctx;

    (void)env;

    if (nargs != 0 || nresults != 1) {
        return ngx_http_wasm_runtime_bad_signature(
            "bad ngx_wasm_yield signature");
    }

    ctx = wasmtime_context_get_data(wasmtime_caller_context(caller));
    if ((ctx->abi.capabilities & NGX_HTTP_WASM_ABI_CAP_YIELD) == 0) {
        return ngx_http_wasm_runtime_phase_forbidden(
            "ngx_wasm_yield not allowed in this phase");
    }

    ctx->yielded = 1;

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = NGX_HTTP_WASM_OK;

    return NULL;
}

ngx_int_t ngx_http_wasm_runtime_run(ngx_http_wasm_exec_ctx_t *ctx) {
    ngx_http_wasm_cached_module_t *module;
    ngx_http_wasm_resume_state_t *resume;
    ngx_http_wasm_runtime_state_t *rt;
    wasmtime_context_t *context;
    wasmtime_extern_t item;
    wasmtime_call_future_t *future;
    wasmtime_error_t *error;
    bool complete;
    bool new_store;

    rt = ctx->runtime;
    if (rt == NULL || rt->engine == NULL || rt->linker == NULL) {
        ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
        ngx_log_error(NGX_LOG_ERR,
                      ngx_http_wasm_runtime_exec_log(ctx),
                      0,
                      "ngx_wasm: runtime not initialized");
        return NGX_ERROR;
    }

    module = ctx->conf->module;
    if (module == NULL || module->module == NULL) {
        ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
        ngx_log_error(NGX_LOG_ERR,
                      ngx_http_wasm_runtime_exec_log(ctx),
                      0,
                      "ngx_wasm: module \"%V\" not preloaded",
                      &ctx->conf->module_path);
        return NGX_ERROR;
    }

    resume = ctx->resume_state;
    if (resume == NULL) {
        resume =
            ngx_pcalloc(ngx_http_wasm_runtime_exec_pool(ctx), sizeof(*resume));
        if (resume == NULL) {
            ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
            return NGX_ERROR;
        }

        ctx->resume_state = resume;
    }

    new_store = (resume->store == NULL);
    if (new_store) {
        resume->store = wasmtime_store_new(rt->engine, ctx, NULL);
    }

    if (resume->store == NULL) {
        ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
        ngx_log_error(NGX_LOG_ERR,
                      ngx_http_wasm_runtime_exec_log(ctx),
                      0,
                      "ngx_wasm: failed to create Wasmtime store");
        return NGX_ERROR;
    }

    context = wasmtime_store_context(resume->store);
    if (new_store && ctx->fuel_limit > 0) {
        error = wasmtime_context_set_fuel(context, ctx->fuel_limit);
        if (error != NULL) {
            ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
            ngx_http_wasm_runtime_log_error(ngx_http_wasm_runtime_exec_log(ctx),
                                            "failed to set fuel",
                                            error);
            return NGX_ERROR;
        }
    }

    ngx_http_wasm_runtime_begin_run(ctx);

    if (!resume->instance_ready) {
        if (resume->future == NULL) {
            if (ngx_http_wasm_runtime_prepare_async_yield(ctx, context) !=
                NGX_OK) {
                ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
                return NGX_ERROR;
            }

            if (ngx_http_wasm_runtime_prepare_async_op(
                    ctx, resume, NGX_HTTP_WASM_FUTURE_INSTANTIATE) != NGX_OK) {
                return NGX_ERROR;
            }

            resume->future =
                wasmtime_linker_instantiate_async(rt->linker,
                                                  context,
                                                  module->module,
                                                  &resume->instance,
                                                  &resume->trap,
                                                  &resume->error);
            if (resume->future == NULL) {
                ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
                if (resume->error != NULL) {
                    ngx_http_wasm_runtime_log_error(
                        ngx_http_wasm_runtime_exec_log(ctx),
                        "failed to start async instantiation",
                        resume->error);
                    resume->error = NULL;
                } else if (resume->trap != NULL) {
                    ngx_http_wasm_runtime_log_trap(
                        ngx_http_wasm_runtime_exec_log(ctx),
                        "module start trap",
                        resume->trap);
                    resume->trap = NULL;
                } else {
                    ngx_log_error(NGX_LOG_ERR,
                                  ngx_http_wasm_runtime_exec_log(ctx),
                                  0,
                                  "ngx_wasm: failed to create instantiation "
                                  "future");
                }
                ngx_http_wasm_runtime_clear_async_future(resume);
                return NGX_ERROR;
            }
        }

        complete = wasmtime_call_future_poll(resume->future);
        if (!complete) {
            if (ngx_http_wasm_runtime_update_fuel(
                    ctx,
                    context,
                    "failed to read remaining fuel after async instantiate "
                    "yield") != NGX_OK) {
                ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
                return NGX_ERROR;
            }

            if (ctx->fuel_remaining == 0) {
                ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
                ngx_log_error(NGX_LOG_ERR,
                              ngx_http_wasm_runtime_exec_log(ctx),
                              0,
                              "ngx_wasm: terminal guest interruption during "
                              "instantiation: fuel_limit=%uL "
                              "timeslice_fuel=%uL fuel_remaining=%uL",
                              (unsigned long long)ctx->fuel_limit,
                              (unsigned long long)ctx->timeslice_fuel,
                              (unsigned long long)ctx->fuel_remaining);
                return NGX_ERROR;
            }

            return ngx_http_wasm_runtime_suspend(
                ctx,
                NGX_HTTP_WASM_SUSPEND_RESCHEDULE,
                "timeslice fuel yield during instantiate");
        }

        ngx_http_wasm_runtime_clear_async_future(resume);

        if (ngx_http_wasm_runtime_update_fuel(
                ctx,
                context,
                "failed to read remaining fuel after async instantiation") !=
            NGX_OK) {
            ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
            return NGX_ERROR;
        }

        if (resume->error != NULL) {
            ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
            ngx_http_wasm_runtime_log_error(ngx_http_wasm_runtime_exec_log(ctx),
                                            "failed to instantiate module",
                                            resume->error);
            resume->error = NULL;
            return NGX_ERROR;
        }

        if (resume->trap != NULL) {
            ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
            ngx_http_wasm_runtime_log_trap(ngx_http_wasm_runtime_exec_log(ctx),
                                           "module start trap",
                                           resume->trap);
            resume->trap = NULL;
            return NGX_ERROR;
        }

        resume->instance_ready = 1;
    }

    if (!resume->func_ready) {
        if (!wasmtime_instance_export_get(
                context,
                &resume->instance,
                (const char *)ctx->conf->export_name.data,
                ctx->conf->export_name.len,
                &item) ||
            item.kind != WASMTIME_EXTERN_FUNC) {
            ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
            ngx_log_error(NGX_LOG_ERR,
                          ngx_http_wasm_runtime_exec_log(ctx),
                          0,
                          "ngx_wasm: export \"%V\" not found or not a function",
                          &ctx->conf->export_name);
            return NGX_ERROR;
        }

        resume->func = item.of.func;
        wasmtime_extern_delete(&item);
        resume->func_ready = 1;
    }

    if (resume->future == NULL) {
        if (ngx_http_wasm_runtime_prepare_async_yield(ctx, context) != NGX_OK) {
            ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
            return NGX_ERROR;
        }

        if (ngx_http_wasm_runtime_prepare_async_op(
                ctx, resume, NGX_HTTP_WASM_FUTURE_CALL) != NGX_OK) {
            return NGX_ERROR;
        }

        resume->future = wasmtime_func_call_async(context,
                                                  &resume->func,
                                                  NULL,
                                                  0,
                                                  resume->results,
                                                  1,
                                                  &resume->trap,
                                                  &resume->error);
        if (resume->future == NULL) {
            ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
            if (resume->error != NULL) {
                ngx_http_wasm_runtime_log_error(
                    ngx_http_wasm_runtime_exec_log(ctx),
                    "failed to start async guest call",
                    resume->error);
                resume->error = NULL;
            } else if (resume->trap != NULL) {
                ngx_http_wasm_runtime_log_trap(
                    ngx_http_wasm_runtime_exec_log(ctx),
                    "guest trapped before execution",
                    resume->trap);
                resume->trap = NULL;
            } else {
                ngx_log_error(NGX_LOG_ERR,
                              ngx_http_wasm_runtime_exec_log(ctx),
                              0,
                              "ngx_wasm: failed to create guest call future");
            }
            ngx_http_wasm_runtime_clear_async_future(resume);
            return NGX_ERROR;
        }
    }

    future = resume->future;
    complete = wasmtime_call_future_poll(future);
    if (!complete) {
        if (ngx_http_wasm_runtime_update_fuel(
                ctx,
                context,
                "failed to read remaining fuel after async call yield") !=
            NGX_OK) {
            ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
            return NGX_ERROR;
        }

        if (ctx->fuel_remaining == 0) {
            ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
            ngx_log_error(
                NGX_LOG_ERR,
                ngx_http_wasm_runtime_exec_log(ctx),
                0,
                "ngx_wasm: terminal guest interruption: fuel_limit=%uL "
                "timeslice_fuel=%uL fuel_remaining=%uL",
                (unsigned long long)ctx->fuel_limit,
                (unsigned long long)ctx->timeslice_fuel,
                (unsigned long long)ctx->fuel_remaining);
            return NGX_ERROR;
        }

        return ngx_http_wasm_runtime_suspend(
            ctx, NGX_HTTP_WASM_SUSPEND_RESCHEDULE, "timeslice fuel yield");
    }

    ngx_http_wasm_runtime_clear_async_future(resume);

    if (ngx_http_wasm_runtime_update_fuel(
            ctx, context, "failed to read remaining fuel after async call") !=
        NGX_OK) {
        ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
        return NGX_ERROR;
    }

    if (resume->error != NULL) {
        ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
        ngx_http_wasm_runtime_log_error(ngx_http_wasm_runtime_exec_log(ctx),
                                        "guest call failed",
                                        resume->error);
        resume->error = NULL;
        return NGX_ERROR;
    }

    if (ctx->yielded) {
        return ngx_http_wasm_runtime_suspend(
            ctx,
            (ctx->suspend_kind == NGX_HTTP_WASM_SUSPEND_NONE)
                ? NGX_HTTP_WASM_SUSPEND_RESCHEDULE
                : ctx->suspend_kind,
            (ctx->suspend_kind == NGX_HTTP_WASM_SUSPEND_WAIT_IO)
                ? "waiting for host I/O"
                : "manual yield");
    }

    if (resume->trap != NULL) {
        wasmtime_trap_code_t code;

        if (wasmtime_trap_code(resume->trap, &code) &&
            (code == WASMTIME_TRAP_CODE_OUT_OF_FUEL ||
             code == WASMTIME_TRAP_CODE_INTERRUPT)) {
            ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
            ngx_log_error(
                NGX_LOG_ERR,
                ngx_http_wasm_runtime_exec_log(ctx),
                0,
                "ngx_wasm: terminal guest interruption: fuel_limit=%uL "
                "timeslice_fuel=%uL fuel_remaining=%uL",
                (unsigned long long)ctx->fuel_limit,
                (unsigned long long)ctx->timeslice_fuel,
                (unsigned long long)ctx->fuel_remaining);
            wasm_trap_delete(resume->trap);
            resume->trap = NULL;
            return NGX_ERROR;
        }

        ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
        ngx_http_wasm_runtime_log_trap(
            ngx_http_wasm_runtime_exec_log(ctx), "guest trapped", resume->trap);
        resume->trap = NULL;
        return NGX_ERROR;
    }

    if (resume->results[0].kind != WASMTIME_I32 ||
        resume->results[0].of.i32 != 0) {
        ctx->state = NGX_HTTP_WASM_EXEC_ERROR;
        ngx_log_error(NGX_LOG_ERR,
                      ngx_http_wasm_runtime_exec_log(ctx),
                      0,
                      "ngx_wasm: guest export \"%V\" returned %d",
                      &ctx->conf->export_name,
                      (int)resume->results[0].of.i32);
        return NGX_ERROR;
    }

    ctx->state = NGX_HTTP_WASM_EXEC_DONE;

    return NGX_OK;
}
