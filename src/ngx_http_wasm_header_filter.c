#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <ngx_http_wasm_module_int.h>

static ngx_http_output_header_filter_pt ngx_http_wasm_next_header_filter;

ngx_int_t ngx_http_wasm_header_filter_init_process(void) {
    ngx_http_wasm_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_wasm_header_filter_handler;

    return NGX_OK;
}

ngx_int_t ngx_http_wasm_header_filter_handler(ngx_http_request_t *r) {
    static ngx_str_t phase_name = ngx_string("header filter");
    ngx_http_wasm_conf_t *wcf;
    ngx_http_wasm_ctx_t *ctx;
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_pool_cleanup_t *cln;
    ngx_int_t rc;

    wcf = ngx_http_get_module_loc_conf(r, ngx_http_wasm_module);
    wmcf = ngx_http_get_module_main_conf(r, ngx_http_wasm_module);

    if (wcf == NULL || wcf->header_filter.set != 1 || wmcf == NULL ||
        wmcf->runtime == NULL) {
        return ngx_http_wasm_next_header_filter(r);
    }

    ngx_log_error(NGX_LOG_NOTICE,
                  r->connection->log,
                  0,
                  "ngx_wasm: %V handler module=\"%V\" export=\"%V\"",
                  &phase_name,
                  &wcf->header_filter.module_path,
                  &wcf->header_filter.export_name);

    ctx = ngx_http_get_module_ctx(r, ngx_http_wasm_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(*ctx));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ngx_http_set_ctx(r, ctx, ngx_http_wasm_module);

        cln = ngx_pool_cleanup_add(r->pool, 0);
        if (cln == NULL) {
            return NGX_ERROR;
        }

        cln->handler = ngx_http_wasm_cleanup_ctx;
        cln->data = ctx;
    }

    if (!ctx->header_filter_exec_set) {
        ngx_http_wasm_runtime_init_exec_ctx(&ctx->header_filter_exec,
                                            r,
                                            &wcf->header_filter,
                                            NGX_HTTP_WASM_PHASE_HEADER_FILTER,
                                            wmcf->runtime);
        ctx->header_filter_exec.fuel_limit = wcf->fuel_limit;
        ctx->header_filter_exec.timeslice_fuel = wcf->timeslice_fuel;
        ctx->header_filter_exec.fuel_remaining = wcf->fuel_limit;
        ctx->header_filter_exec_set = 1;
    }

    rc = ngx_http_wasm_runtime_run(&ctx->header_filter_exec);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    if (ctx->header_filter_exec.abi.status_set) {
        r->headers_out.status = ctx->header_filter_exec.abi.status;
    }

    return ngx_http_wasm_next_header_filter(r);
}
