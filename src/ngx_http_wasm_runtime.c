#include <ngx_http_wasm_runtime.h>

void ngx_http_wasm_runtime_init_exec_ctx(ngx_http_wasm_exec_ctx_t *ctx,
                                         ngx_http_request_t *r,
                                         ngx_http_wasm_conf_t *conf) {
    ngx_memzero(ctx, sizeof(*ctx));

    ctx->request = r;
    ctx->conf = conf;

    ngx_http_wasm_abi_init(&ctx->abi, r);
}

ngx_int_t ngx_http_wasm_runtime_run(ngx_http_wasm_exec_ctx_t *ctx) {
    static const u_char log_msg[] = "phase 1 runtime stub";
    static const u_char body[] = "hello from wasm abi stub\n";

    if (ngx_http_wasm_abi_log(&ctx->abi,
                              NGX_HTTP_WASM_LOG_NOTICE,
                              log_msg,
                              sizeof(log_msg) - 1) != NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_RUNTIME_ERROR;
    }

    if (ngx_http_wasm_abi_resp_set_status(&ctx->abi, NGX_HTTP_OK) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_RUNTIME_ERROR;
    }

    if (ngx_http_wasm_abi_resp_write(&ctx->abi, body, sizeof(body) - 1, 0) !=
        NGX_HTTP_WASM_OK) {
        return NGX_HTTP_WASM_RUNTIME_ERROR;
    }

    return NGX_HTTP_WASM_RUNTIME_CONTINUE;
}
