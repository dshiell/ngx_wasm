#include <ngx_http_wasm_runtime.h>

#include <wasm.h>
#include <wasmtime/config.h>
#include <wasmtime/error.h>
#include <wasmtime/extern.h>
#include <wasmtime/func.h>
#include <wasmtime/instance.h>
#include <wasmtime/linker.h>
#include <wasmtime/memory.h>
#include <wasmtime/module.h>
#include <wasmtime/store.h>
#include <wasmtime/trap.h>
#include <wasmtime/val.h>
#include <wasmtime/wat.h>

#define NGX_HTTP_WASM_IMPORT_MODULE "env"
#define NGX_HTTP_WASM_MEMORY_EXPORT "memory"
struct ngx_http_wasm_cached_module_s {
    ngx_str_t module_path;
    wasmtime_module_t *module;
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
static wasm_trap_t *ngx_http_wasm_runtime_get_memory(wasmtime_caller_t *caller,
                                                     uint32_t ptr,
                                                     uint32_t len,
                                                     const u_char **data);
static wasm_trap_t *ngx_http_wasm_runtime_bad_signature(const char *name);
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
static wasm_trap_t *ngx_http_wasm_host_resp_write(void *env,
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
#if (NGX_DARWIN)
    wasmtime_config_macos_use_mach_ports_set(config, false);
#endif

    rt->engine = wasm_engine_new_with_config(config);
    if (rt->engine == NULL) {
        ngx_log_error(NGX_LOG_ERR,
                      cf->log,
                      0,
                      "ngx_wasm: failed to create Wasmtime engine");
        return NGX_ERROR;
    }

    rt->linker = wasmtime_linker_new(rt->engine);
    if (rt->linker == NULL) {
        ngx_log_error(NGX_LOG_ERR,
                      cf->log,
                      0,
                      "ngx_wasm: failed to create Wasmtime linker");
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_define_host_funcs(rt) != NGX_OK) {
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
    ngx_http_wasm_conf_t *conf,
    ngx_http_wasm_runtime_state_t *runtime) {
    ngx_memzero(ctx, sizeof(*ctx));

    ctx->request = r;
    ctx->conf = conf;
    ctx->runtime = runtime;
    ctx->fuel_limit = conf->fuel_limit;
    ctx->timeslice_fuel = conf->timeslice_fuel;
    ctx->fuel_remaining = conf->fuel_limit;

    ngx_http_wasm_abi_init(&ctx->abi, r);
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
            "ngx_wasm_resp_write",
            wasm_functype_new_2_1(wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32),
                                  wasm_valtype_new(WASM_I32)),
            ngx_http_wasm_host_resp_write) != NGX_OK) {
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
            ctx->request->connection->log, phase, error);
        return NGX_ERROR;
    }

    ctx->fuel_remaining = fuel;

    return NGX_OK;
}

static wasm_trap_t *ngx_http_wasm_runtime_bad_signature(const char *name) {
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
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 =
        ngx_http_wasm_abi_resp_set_status(&ctx->abi, args[0].of.i32);

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
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ngx_http_wasm_abi_resp_write(
        &ctx->abi, data, (size_t)args[1].of.i32, 1);

    return NULL;
}

ngx_int_t ngx_http_wasm_runtime_run(ngx_http_wasm_exec_ctx_t *ctx) {
    ngx_http_wasm_cached_module_t *module;
    ngx_http_wasm_runtime_state_t *rt;
    wasmtime_store_t *store;
    wasmtime_context_t *context;
    wasmtime_instance_t instance;
    wasmtime_extern_t item;
    wasmtime_val_t result;
    wasmtime_error_t *error;
    wasm_trap_t *trap;
    uint64_t fuel_slice;

    rt = ctx->runtime;
    if (rt == NULL || rt->engine == NULL || rt->linker == NULL) {
        ngx_log_error(NGX_LOG_ERR,
                      ctx->request->connection->log,
                      0,
                      "ngx_wasm: runtime not initialized");
        return NGX_ERROR;
    }

    module = ctx->conf->module;
    if (module == NULL || module->module == NULL) {
        ngx_log_error(NGX_LOG_ERR,
                      ctx->request->connection->log,
                      0,
                      "ngx_wasm: module \"%V\" not preloaded",
                      &ctx->conf->module_path);
        return NGX_ERROR;
    }

    store = wasmtime_store_new(rt->engine, ctx, NULL);
    if (store == NULL) {
        ngx_log_error(NGX_LOG_ERR,
                      ctx->request->connection->log,
                      0,
                      "ngx_wasm: failed to create Wasmtime store");
        return NGX_ERROR;
    }

    context = wasmtime_store_context(store);
    fuel_slice = ctx->fuel_remaining;
    if (ctx->timeslice_fuel > 0 && ctx->timeslice_fuel < fuel_slice) {
        fuel_slice = ctx->timeslice_fuel;
    }

    error = wasmtime_context_set_fuel(context, fuel_slice);
    if (error != NULL) {
        ngx_http_wasm_runtime_log_error(
            ctx->request->connection->log, "failed to set fuel", error);
        wasmtime_store_delete(store);
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_update_fuel(
            ctx, context, "failed to read remaining fuel after setup") !=
        NGX_OK) {
        wasmtime_store_delete(store);
        return NGX_ERROR;
    }

    trap = NULL;
    error = wasmtime_linker_instantiate(
        rt->linker, context, module->module, &instance, &trap);
    if (error != NULL) {
        ngx_http_wasm_runtime_log_error(ctx->request->connection->log,
                                        "failed to instantiate module",
                                        error);
        wasmtime_store_delete(store);
        return NGX_ERROR;
    }

    if (trap != NULL) {
        ngx_http_wasm_runtime_log_trap(
            ctx->request->connection->log, "module start trap", trap);
        wasmtime_store_delete(store);
        return NGX_ERROR;
    }

    if (!wasmtime_instance_export_get(context,
                                      &instance,
                                      (const char *)ctx->conf->export_name.data,
                                      ctx->conf->export_name.len,
                                      &item) ||
        item.kind != WASMTIME_EXTERN_FUNC) {
        ngx_log_error(NGX_LOG_ERR,
                      ctx->request->connection->log,
                      0,
                      "ngx_wasm: export \"%V\" not found or not a function",
                      &ctx->conf->export_name);
        wasmtime_store_delete(store);
        return NGX_ERROR;
    }

    trap = NULL;
    error =
        wasmtime_func_call(context, &item.of.func, NULL, 0, &result, 1, &trap);
    wasmtime_extern_delete(&item);

    if (error != NULL) {
        ngx_http_wasm_runtime_log_error(
            ctx->request->connection->log, "guest call failed", error);
        wasmtime_store_delete(store);
        return NGX_ERROR;
    }

    if (trap != NULL) {
        wasmtime_trap_code_t code;

        if (wasmtime_trap_code(trap, &code) &&
            (code == WASMTIME_TRAP_CODE_OUT_OF_FUEL ||
             code == WASMTIME_TRAP_CODE_INTERRUPT)) {
            if (ngx_http_wasm_runtime_update_fuel(
                    ctx,
                    context,
                    "failed to read remaining fuel after interruption") !=
                NGX_OK) {
                wasmtime_store_delete(store);
                return NGX_ERROR;
            }

            ngx_log_error(NGX_LOG_ERR,
                          ctx->request->connection->log,
                          0,
                          "ngx_wasm: guest interrupted: fuel_limit=%uL "
                          "timeslice_fuel=%uL fuel_remaining=%uL",
                          (unsigned long long)ctx->fuel_limit,
                          (unsigned long long)ctx->timeslice_fuel,
                          (unsigned long long)ctx->fuel_remaining);
            wasm_trap_delete(trap);
            wasmtime_store_delete(store);
            return NGX_ERROR;
        }

        ngx_http_wasm_runtime_log_trap(
            ctx->request->connection->log, "guest trapped", trap);
        wasmtime_store_delete(store);
        return NGX_ERROR;
    }

    if (ngx_http_wasm_runtime_update_fuel(
            ctx, context, "failed to read remaining fuel after guest call") !=
        NGX_OK) {
        wasmtime_store_delete(store);
        return NGX_ERROR;
    }

    if (result.kind != WASMTIME_I32 || result.of.i32 != 0) {
        ngx_log_error(NGX_LOG_ERR,
                      ctx->request->connection->log,
                      0,
                      "ngx_wasm: guest export \"%V\" returned %d",
                      &ctx->conf->export_name,
                      (int)result.of.i32);
        wasmtime_store_delete(store);
        return NGX_ERROR;
    }

    wasmtime_store_delete(store);

    return NGX_OK;
}
