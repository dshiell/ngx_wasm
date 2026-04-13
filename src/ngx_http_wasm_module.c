#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <ngx_http_wasm_module_int.h>
#include <ngx_http_wasm_runtime.h>

static ngx_int_t ngx_http_wasm_content_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_wasm_server_rewrite_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_wasm_rewrite_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_wasm_run_request(ngx_http_request_t *r,
                                           ngx_http_wasm_ctx_t *ctx);
static ngx_int_t ngx_http_wasm_run_phase(ngx_http_request_t *r,
                                         ngx_http_wasm_conf_t *wcf,
                                         ngx_http_wasm_phase_conf_t *phase,
                                         ngx_str_t *phase_name);
static ngx_http_wasm_ctx_t *
ngx_http_wasm_get_or_create_ctx(ngx_http_request_t *r,
                                ngx_http_wasm_conf_t *wcf,
                                ngx_http_wasm_main_conf_t *wmcf,
                                ngx_http_wasm_phase_conf_t *phase);
static ngx_int_t ngx_http_wasm_configure_phase(ngx_conf_t *cf,
                                               ngx_http_wasm_phase_conf_t *dst);
static void ngx_http_wasm_merge_phase_conf(ngx_http_wasm_phase_conf_t *parent,
                                           ngx_http_wasm_phase_conf_t *child);
static void ngx_http_wasm_cleanup_main_conf(void *data);
static char *
ngx_http_wasm_content_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *
ngx_http_wasm_server_rewrite_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *
ngx_http_wasm_header_filter_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *
ngx_http_wasm_rewrite_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_wasm_request_body_buffer_size(ngx_conf_t *cf,
                                                    ngx_command_t *cmd,
                                                    void *conf);
static char *
ngx_http_wasm_fuel_limit(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *
ngx_http_wasm_timeslice_fuel(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_wasm_install_content_handler(ngx_conf_t *cf);
static ngx_int_t ngx_http_wasm_postconfiguration(ngx_conf_t *cf);
static ngx_int_t ngx_http_wasm_init_module(ngx_cycle_t *cycle);
static ngx_int_t ngx_http_wasm_init_process(ngx_cycle_t *cycle);
static void ngx_http_wasm_exit_process(ngx_cycle_t *cycle);
static void *ngx_http_wasm_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_wasm_init_main_conf(ngx_conf_t *cf, void *conf);
static void *ngx_http_wasm_create_srv_conf(ngx_conf_t *cf);
static char *
ngx_http_wasm_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child);
static void *ngx_http_wasm_create_loc_conf(ngx_conf_t *cf);
static char *
ngx_http_wasm_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static ngx_command_t ngx_http_wasm_commands[] = {

    {ngx_string("content_by_wasm"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE2,
     ngx_http_wasm_content_by,
     NGX_HTTP_LOC_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("rewrite_by_wasm"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE2,
     ngx_http_wasm_rewrite_by,
     NGX_HTTP_LOC_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("server_rewrite_by_wasm"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_CONF_TAKE2,
     ngx_http_wasm_server_rewrite_by,
     NGX_HTTP_SRV_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("header_filter_by_wasm"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE2,
     ngx_http_wasm_header_filter_by,
     NGX_HTTP_LOC_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("wasm_fuel_limit"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE1,
     ngx_http_wasm_fuel_limit,
     NGX_HTTP_LOC_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("wasm_request_body_buffer_size"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE1,
     ngx_http_wasm_request_body_buffer_size,
     NGX_HTTP_LOC_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("wasm_timeslice_fuel"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE1,
     ngx_http_wasm_timeslice_fuel,
     NGX_HTTP_LOC_CONF_OFFSET,
     0,
     NULL},

    ngx_null_command};

static ngx_http_module_t ngx_http_wasm_module_ctx = {
    NULL,                            /* preconfiguration */
    ngx_http_wasm_postconfiguration, /* postconfiguration */

    ngx_http_wasm_create_main_conf, /* create main configuration */
    ngx_http_wasm_init_main_conf,   /* init main configuration */

    ngx_http_wasm_create_srv_conf, /* create server configuration */
    ngx_http_wasm_merge_srv_conf,  /* merge server configuration */

    ngx_http_wasm_create_loc_conf, /* create location configuration */
    ngx_http_wasm_merge_loc_conf   /* merge location configuration */
};

ngx_module_t ngx_http_wasm_module = {
    NGX_MODULE_V1,
    &ngx_http_wasm_module_ctx,  /* module context */
    ngx_http_wasm_commands,     /* module directives */
    NGX_HTTP_MODULE,            /* module type */
    NULL,                       /* init master */
    ngx_http_wasm_init_module,  /* init module */
    ngx_http_wasm_init_process, /* init process */
    NULL,                       /* init thread */
    NULL,                       /* exit thread */
    ngx_http_wasm_exit_process, /* exit process */
    NULL,                       /* exit master */
    NGX_MODULE_V1_PADDING};

static void *ngx_http_wasm_create_conf(ngx_conf_t *cf) {
    ngx_http_wasm_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_wasm_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->content.set = NGX_CONF_UNSET;
    conf->rewrite.set = NGX_CONF_UNSET;
    conf->server_rewrite.set = NGX_CONF_UNSET;
    conf->header_filter.set = NGX_CONF_UNSET;
    conf->fuel_limit = NGX_CONF_UNSET_UINT;
    conf->timeslice_fuel = NGX_CONF_UNSET_UINT;
    conf->request_body_buffer_size = NGX_CONF_UNSET_SIZE;

    return conf;
}

static char *ngx_http_wasm_merge_conf(ngx_conf_t *cf,
                                      ngx_http_wasm_conf_t *parent,
                                      ngx_http_wasm_conf_t *child) {
    (void)cf;

    ngx_conf_merge_uint_value(child->fuel_limit,
                              parent->fuel_limit,
                              NGX_HTTP_WASM_DEFAULT_FUEL_LIMIT);
    ngx_conf_merge_uint_value(child->timeslice_fuel,
                              parent->timeslice_fuel,
                              NGX_HTTP_WASM_DEFAULT_TIMESLICE_FUEL);
    ngx_conf_merge_size_value(child->request_body_buffer_size,
                              parent->request_body_buffer_size,
                              NGX_HTTP_WASM_DEFAULT_REQUEST_BODY_BUFFER_SIZE);

    ngx_http_wasm_merge_phase_conf(&parent->content, &child->content);
    ngx_http_wasm_merge_phase_conf(&parent->rewrite, &child->rewrite);
    ngx_http_wasm_merge_phase_conf(&parent->server_rewrite,
                                   &child->server_rewrite);
    ngx_http_wasm_merge_phase_conf(&parent->header_filter,
                                   &child->header_filter);

    return NGX_CONF_OK;
}

static void ngx_http_wasm_merge_phase_conf(ngx_http_wasm_phase_conf_t *parent,
                                           ngx_http_wasm_phase_conf_t *child) {
    ngx_conf_merge_value(child->set, parent->set, 0);

    if (child->module == NULL && parent->module != NULL) {
        child->module = parent->module;
    }

    if (child->module_path.data == NULL && parent->module_path.data != NULL) {
        child->module_path = parent->module_path;
    }

    if (child->export_name.data == NULL && parent->export_name.data != NULL) {
        child->export_name = parent->export_name;
    }
}

static void *ngx_http_wasm_create_main_conf(ngx_conf_t *cf) {
    ngx_pool_cleanup_t *cln;
    ngx_http_wasm_main_conf_t *wmcf;

    wmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_wasm_main_conf_t));
    if (wmcf == NULL) {
        return NULL;
    }

    wmcf->modules =
        ngx_array_create(cf->pool, 4, sizeof(ngx_http_wasm_cached_module_t *));
    if (wmcf->modules == NULL) {
        return NULL;
    }

    if (ngx_http_wasm_runtime_init(cf, wmcf) != NGX_OK) {
        return NULL;
    }

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        ngx_http_wasm_runtime_destroy(wmcf);
        return NULL;
    }

    cln->handler = ngx_http_wasm_cleanup_main_conf;
    cln->data = wmcf;

    return wmcf;
}

static char *ngx_http_wasm_init_main_conf(ngx_conf_t *cf, void *conf) {
    ngx_http_wasm_main_conf_t *wmcf = conf;

    (void)cf;

    if (wmcf->modules == NULL || wmcf->runtime == NULL) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static void *ngx_http_wasm_create_srv_conf(ngx_conf_t *cf) {
    return ngx_http_wasm_create_conf(cf);
}

static char *
ngx_http_wasm_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child) {
    return ngx_http_wasm_merge_conf(cf, parent, child);
}

static void *ngx_http_wasm_create_loc_conf(ngx_conf_t *cf) {
    return ngx_http_wasm_create_conf(cf);
}

static char *
ngx_http_wasm_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child) {
    ngx_http_wasm_conf_t *prev = parent;
    ngx_http_wasm_conf_t *conf = child;
    char *rc;

    rc = ngx_http_wasm_merge_conf(cf, prev, conf);
    if (rc != NGX_CONF_OK) {
        return rc;
    }

    if (conf->content.set == 1) {
        if (ngx_http_wasm_install_content_handler(cf) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_wasm_install_content_handler(ngx_conf_t *cf) {
    ngx_http_core_loc_conf_t *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_wasm_content_handler;

    return NGX_OK;
}

static ngx_int_t ngx_http_wasm_postconfiguration(ngx_conf_t *cf) {
    ngx_http_handler_pt *h;
    ngx_http_core_main_conf_t *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_SERVER_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_wasm_server_rewrite_handler;

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_wasm_rewrite_handler;

    return NGX_OK;
}

static ngx_int_t ngx_http_wasm_init_module(ngx_cycle_t *cycle) {
    (void)cycle;

    return NGX_OK;
}

static ngx_int_t ngx_http_wasm_init_process(ngx_cycle_t *cycle) {
    (void)cycle;

    return ngx_http_wasm_header_filter_init_process();
}

static void ngx_http_wasm_exit_process(ngx_cycle_t *cycle) {
    ngx_http_wasm_main_conf_t *wmcf;

    wmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_wasm_module);
    if (wmcf == NULL) {
        return;
    }

    ngx_http_wasm_runtime_destroy(wmcf);
}

static ngx_int_t
ngx_http_wasm_configure_phase(ngx_conf_t *cf, ngx_http_wasm_phase_conf_t *dst) {
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_str_t *value;

    if (dst->set == 1) {
        return NGX_DECLINED;
    }

    value = cf->args->elts;

    dst->module_path = value[1];
    dst->export_name = value[2];
    dst->set = 1;

    if (ngx_conf_full_name(cf->cycle, &dst->module_path, 0) != NGX_OK) {
        return NGX_ERROR;
    }

    wmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_wasm_module);
    if (wmcf == NULL) {
        return NGX_ERROR;
    }

    dst->module =
        ngx_http_wasm_runtime_get_or_load(cf, wmcf, &dst->module_path);
    if (dst->module == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

static char *
ngx_http_wasm_content_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_conf_t *wcf;

    (void)cmd;
    wcf = conf;

    switch (ngx_http_wasm_configure_phase(cf, &wcf->content)) {
        case NGX_OK:
            return NGX_CONF_OK;
        case NGX_DECLINED:
            return "is duplicate";
        default:
            return NGX_CONF_ERROR;
    }
}

static char *ngx_http_wasm_server_rewrite_by(ngx_conf_t *cf,
                                             ngx_command_t *cmd,
                                             void *conf) {
    ngx_http_wasm_conf_t *wcf;

    (void)cmd;
    wcf = conf;

    switch (ngx_http_wasm_configure_phase(cf, &wcf->server_rewrite)) {
        case NGX_OK:
            return NGX_CONF_OK;
        case NGX_DECLINED:
            return "is duplicate";
        default:
            return NGX_CONF_ERROR;
    }
}

static char *
ngx_http_wasm_header_filter_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_conf_t *wcf;

    (void)cmd;
    wcf = conf;

    switch (ngx_http_wasm_configure_phase(cf, &wcf->header_filter)) {
        case NGX_OK:
            return NGX_CONF_OK;
        case NGX_DECLINED:
            return "is duplicate";
        default:
            return NGX_CONF_ERROR;
    }
}

static char *
ngx_http_wasm_rewrite_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_conf_t *wcf;

    (void)cmd;
    wcf = conf;

    switch (ngx_http_wasm_configure_phase(cf, &wcf->rewrite)) {
        case NGX_OK:
            return NGX_CONF_OK;
        case NGX_DECLINED:
            return "is duplicate";
        default:
            return NGX_CONF_ERROR;
    }
}

static char *ngx_http_wasm_request_body_buffer_size(ngx_conf_t *cf,
                                                    ngx_command_t *cmd,
                                                    void *conf) {
    ngx_http_wasm_conf_t *wcf;
    ngx_str_t *value;
    off_t size;

    (void)cmd;
    wcf = conf;

    value = cf->args->elts;
    size = ngx_parse_size(&value[1]);
    if (size == NGX_ERROR || size < 0) {
        ngx_conf_log_error(NGX_LOG_EMERG,
                           cf,
                           0,
                           "invalid wasm_request_body_buffer_size value \"%V\"",
                           &value[1]);
        return NGX_CONF_ERROR;
    }

    wcf->request_body_buffer_size = (size_t)size;

    return NGX_CONF_OK;
}

static char *
ngx_http_wasm_fuel_limit(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_conf_t *wcf;
    ngx_str_t *value;
    ngx_int_t n;

    (void)cmd;
    wcf = conf;

    value = cf->args->elts;
    n = ngx_atoi(value[1].data, value[1].len);
    if (n == NGX_ERROR || n < 0) {
        ngx_conf_log_error(NGX_LOG_EMERG,
                           cf,
                           0,
                           "invalid wasm_fuel_limit value \"%V\"",
                           &value[1]);
        return NGX_CONF_ERROR;
    }

    wcf->fuel_limit = (ngx_uint_t)n;

    return NGX_CONF_OK;
}

static char *
ngx_http_wasm_timeslice_fuel(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_conf_t *wcf;
    ngx_str_t *value;
    ngx_int_t n;

    (void)cmd;
    wcf = conf;

    value = cf->args->elts;
    n = ngx_atoi(value[1].data, value[1].len);
    if (n == NGX_ERROR || n <= 0) {
        ngx_conf_log_error(NGX_LOG_EMERG,
                           cf,
                           0,
                           "invalid wasm_timeslice_fuel value \"%V\"",
                           &value[1]);
        return NGX_CONF_ERROR;
    }

    wcf->timeslice_fuel = (ngx_uint_t)n;

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_wasm_content_handler(ngx_http_request_t *r) {
    static ngx_str_t phase_name = ngx_string("content");
    ngx_http_wasm_conf_t *wcf;

    wcf = ngx_http_get_module_loc_conf(r, ngx_http_wasm_module);
    if (wcf == NULL) {
        return NGX_DECLINED;
    }

    return ngx_http_wasm_run_phase(r, wcf, &wcf->content, &phase_name);
}

static ngx_int_t ngx_http_wasm_server_rewrite_handler(ngx_http_request_t *r) {
    static ngx_str_t phase_name = ngx_string("server rewrite");
    ngx_http_wasm_conf_t *wcf;

    wcf = ngx_http_get_module_srv_conf(r, ngx_http_wasm_module);
    if (wcf == NULL || wcf->server_rewrite.set != 1) {
        return NGX_DECLINED;
    }

    return ngx_http_wasm_run_phase(r, wcf, &wcf->server_rewrite, &phase_name);
}

static ngx_int_t ngx_http_wasm_rewrite_handler(ngx_http_request_t *r) {
    static ngx_str_t phase_name = ngx_string("rewrite");
    ngx_http_wasm_conf_t *wcf;

    wcf = ngx_http_get_module_loc_conf(r, ngx_http_wasm_module);
    if (wcf == NULL || wcf->rewrite.set != 1) {
        return NGX_DECLINED;
    }

    return ngx_http_wasm_run_phase(r, wcf, &wcf->rewrite, &phase_name);
}

static ngx_int_t ngx_http_wasm_run_phase(ngx_http_request_t *r,
                                         ngx_http_wasm_conf_t *wcf,
                                         ngx_http_wasm_phase_conf_t *phase,
                                         ngx_str_t *phase_name) {
    ngx_http_wasm_ctx_t *ctx;
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_int_t rc;

    wmcf = ngx_http_get_module_main_conf(r, ngx_http_wasm_module);
    if (phase->set != 1 || wmcf == NULL || wmcf->runtime == NULL) {
        return NGX_DECLINED;
    }

    ngx_log_error(NGX_LOG_NOTICE,
                  r->connection->log,
                  0,
                  "ngx_wasm: %V handler module=\"%V\" export=\"%V\"",
                  phase_name,
                  &phase->module_path,
                  &phase->export_name);

    ctx = ngx_http_wasm_get_or_create_ctx(r, wcf, wmcf, phase);
    if (ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ctx->exec.conf != phase) {
        ngx_log_error(
            NGX_LOG_ERR,
            r->connection->log,
            0,
            "ngx_wasm: request context already bound to a different phase");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    rc = ngx_http_wasm_prepare_request_body(r, wcf, ctx);
    if (rc != NGX_OK) {
        if (rc == NGX_DONE && ctx->request_body_async) {
            ngx_http_finalize_request(r, NGX_DONE);
        }

        return rc;
    }

    return ngx_http_wasm_run_request(r, ctx);
}

static ngx_http_wasm_ctx_t *
ngx_http_wasm_get_or_create_ctx(ngx_http_request_t *r,
                                ngx_http_wasm_conf_t *wcf,
                                ngx_http_wasm_main_conf_t *wmcf,
                                ngx_http_wasm_phase_conf_t *phase) {
    ngx_pool_cleanup_t *cln;
    ngx_http_wasm_ctx_t *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_wasm_module);
    if (ctx != NULL) {
        return ctx;
    }

    ctx = ngx_pcalloc(r->pool, sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }

    ngx_http_set_ctx(r, ctx, ngx_http_wasm_module);

    cln = ngx_pool_cleanup_add(r->pool, 0);
    if (cln == NULL) {
        return NULL;
    }

    cln->handler = ngx_http_wasm_cleanup_ctx;
    cln->data = ctx;

    if (phase == &wcf->rewrite && wcf->rewrite.set == 1) {
        ngx_http_wasm_runtime_init_exec_ctx(&ctx->exec,
                                            r,
                                            &wcf->rewrite,
                                            NGX_HTTP_WASM_PHASE_REWRITE,
                                            wmcf->runtime);
        ctx->exec.fuel_limit = wcf->fuel_limit;
        ctx->exec.timeslice_fuel = wcf->timeslice_fuel;
        ctx->exec.fuel_remaining = wcf->fuel_limit;
        return ctx;
    }

    if (phase == &wcf->server_rewrite && wcf->server_rewrite.set == 1) {
        ngx_http_wasm_runtime_init_exec_ctx(&ctx->exec,
                                            r,
                                            &wcf->server_rewrite,
                                            NGX_HTTP_WASM_PHASE_SERVER_REWRITE,
                                            wmcf->runtime);
        ctx->exec.fuel_limit = wcf->fuel_limit;
        ctx->exec.timeslice_fuel = wcf->timeslice_fuel;
        ctx->exec.fuel_remaining = wcf->fuel_limit;
        return ctx;
    }

    if (phase == &wcf->content && wcf->content.set == 1) {
        ngx_http_wasm_runtime_init_exec_ctx(&ctx->exec,
                                            r,
                                            &wcf->content,
                                            NGX_HTTP_WASM_PHASE_CONTENT,
                                            wmcf->runtime);
        ctx->exec.fuel_limit = wcf->fuel_limit;
        ctx->exec.timeslice_fuel = wcf->timeslice_fuel;
        ctx->exec.fuel_remaining = wcf->fuel_limit;
        return ctx;
    }

    return NULL;
}

void ngx_http_wasm_resume_handler(ngx_http_request_t *r) {
    ngx_http_wasm_ctx_t *ctx;
    ngx_int_t rc;

    ctx = ngx_http_get_module_ctx(r, ngx_http_wasm_module);
    if (ctx == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    if (ctx->request_body_status != NGX_OK) {
        ngx_http_finalize_request(r, ctx->request_body_status);
        return;
    }

    ngx_log_error(NGX_LOG_NOTICE,
                  r->connection->log,
                  0,
                  "ngx_wasm: resuming suspended request");

    rc = ngx_http_wasm_run_request(r, ctx);
    if (rc == NGX_DONE) {
        return;
    }

    if (rc == NGX_DECLINED) {
        r->write_event_handler = ngx_http_core_run_phases;
        ngx_http_core_run_phases(r);
        return;
    }

    ngx_http_finalize_request(r, rc);
}

static ngx_int_t ngx_http_wasm_run_request(ngx_http_request_t *r,
                                           ngx_http_wasm_ctx_t *ctx) {
    ngx_int_t rc;

    rc = ngx_http_wasm_runtime_run(&ctx->exec);
    if (rc == NGX_AGAIN) {
        r->write_event_handler = ngx_http_wasm_resume_handler;

        if (ngx_http_post_request(r, NULL) != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        if (!ctx->waiting &&
            ctx->exec.phase_kind == NGX_HTTP_WASM_PHASE_CONTENT) {
            ctx->waiting = 1;
            r->main->count++;
        }

        if (!ctx->waiting) {
            ctx->waiting = 1;
        }

        return NGX_DONE;
    }

    if (rc != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ctx->waiting = 0;

    if ((ctx->exec.phase_kind == NGX_HTTP_WASM_PHASE_REWRITE ||
         ctx->exec.phase_kind == NGX_HTTP_WASM_PHASE_SERVER_REWRITE) &&
        !ctx->exec.abi.status_set && !ctx->exec.abi.body_set &&
        !ctx->exec.abi.content_type_set) {
        ngx_http_set_ctx(r, NULL, ngx_http_wasm_module);
        return NGX_DECLINED;
    }

    rc = ngx_http_wasm_abi_send_response(&ctx->exec.abi);
    if (rc == NGX_ERROR) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    return rc;
}

void ngx_http_wasm_cleanup_ctx(void *data) {
    ngx_http_wasm_ctx_t *ctx = data;

    if (ctx == NULL) {
        return;
    }

    ngx_http_wasm_runtime_cleanup_exec_ctx(&ctx->exec);

    if (ctx->header_filter_exec_set) {
        ngx_http_wasm_runtime_cleanup_exec_ctx(&ctx->header_filter_exec);
    }
}

static void ngx_http_wasm_cleanup_main_conf(void *data) {
    ngx_http_wasm_main_conf_t *wmcf = data;

    ngx_http_wasm_runtime_destroy(wmcf);
}
