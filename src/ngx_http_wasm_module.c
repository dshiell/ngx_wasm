#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <ngx_http_wasm_runtime.h>

static ngx_int_t ngx_http_wasm_content_handler(ngx_http_request_t *r);
static char *
ngx_http_wasm_content_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *
ngx_http_wasm_fuel_limit(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *
ngx_http_wasm_timeslice_fuel(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_wasm_install_content_handler(ngx_conf_t *cf);
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

    {ngx_string("wasm_fuel_limit"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE1,
     ngx_http_wasm_fuel_limit,
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
    NULL, /* preconfiguration */
    NULL, /* postconfiguration */

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

    conf->set = NGX_CONF_UNSET;
    conf->fuel_limit = NGX_CONF_UNSET_UINT;
    conf->timeslice_fuel = NGX_CONF_UNSET_UINT;

    return conf;
}

static char *ngx_http_wasm_merge_conf(ngx_conf_t *cf,
                                      ngx_http_wasm_conf_t *parent,
                                      ngx_http_wasm_conf_t *child) {
    (void)cf;

    ngx_conf_merge_value(child->set, parent->set, 0);
    ngx_conf_merge_uint_value(child->fuel_limit,
                              parent->fuel_limit,
                              NGX_HTTP_WASM_DEFAULT_FUEL_LIMIT);
    ngx_conf_merge_uint_value(child->timeslice_fuel,
                              parent->timeslice_fuel,
                              NGX_HTTP_WASM_DEFAULT_TIMESLICE_FUEL);
    if (child->module == NULL && parent->module != NULL) {
        child->module = parent->module;
    }

    if (child->module_path.data == NULL && parent->module_path.data != NULL) {
        child->module_path = parent->module_path;
    }

    if (child->export_name.data == NULL && parent->export_name.data != NULL) {
        child->export_name = parent->export_name;
    }

    return NGX_CONF_OK;
}

static void *ngx_http_wasm_create_main_conf(ngx_conf_t *cf) {
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

    if (conf->set == 1) {
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

static ngx_int_t ngx_http_wasm_init_module(ngx_cycle_t *cycle) {
    (void)cycle;

    return NGX_OK;
}

static ngx_int_t ngx_http_wasm_init_process(ngx_cycle_t *cycle) {
    (void)cycle;

    return NGX_OK;
}

static void ngx_http_wasm_exit_process(ngx_cycle_t *cycle) {
    ngx_http_wasm_main_conf_t *wmcf;

    wmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_wasm_module);
    if (wmcf == NULL) {
        return;
    }

    ngx_http_wasm_runtime_destroy(wmcf);
}

static char *
ngx_http_wasm_content_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_conf_t *wcf;
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_str_t *value;

    (void)cmd;
    wcf = conf;

    if (wcf->set == 1) {
        return "is duplicate";
    }

    value = cf->args->elts;

    wcf->module_path = value[1];
    wcf->export_name = value[2];
    wcf->set = 1;

    if (ngx_conf_full_name(cf->cycle, &wcf->module_path, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    wmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_wasm_module);
    if (wmcf == NULL) {
        return NGX_CONF_ERROR;
    }

    wcf->module =
        ngx_http_wasm_runtime_get_or_load(cf, wmcf, &wcf->module_path);
    if (wcf->module == NULL) {
        return NGX_CONF_ERROR;
    }

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
    ngx_http_wasm_conf_t *wcf;
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_http_wasm_exec_ctx_t exec;
    ngx_int_t rc;

    wcf = ngx_http_get_module_loc_conf(r, ngx_http_wasm_module);
    wmcf = ngx_http_get_module_main_conf(r, ngx_http_wasm_module);

    if (wcf->set != 1 || wmcf == NULL || wmcf->runtime == NULL) {
        return NGX_DECLINED;
    }

    ngx_log_error(NGX_LOG_NOTICE,
                  r->connection->log,
                  0,
                  "ngx_wasm: content handler module=\"%V\" export=\"%V\"",
                  &wcf->module_path,
                  &wcf->export_name);

    if (!(r->method & (NGX_HTTP_GET | NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

    ngx_http_wasm_runtime_init_exec_ctx(&exec, r, wcf, wmcf->runtime);

    rc = ngx_http_wasm_runtime_run(&exec);
    if (rc != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    rc = ngx_http_wasm_abi_send_response(&exec.abi);
    if (rc == NGX_ERROR) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    return rc;
}
