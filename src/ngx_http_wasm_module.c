#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <ngx_http_wasm_runtime.h>

static ngx_int_t ngx_http_wasm_content_handler(ngx_http_request_t *r);
static char *
ngx_http_wasm_content_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_wasm_install_content_handler(ngx_conf_t *cf);
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
    &ngx_http_wasm_module_ctx, /* module context */
    ngx_http_wasm_commands,    /* module directives */
    NGX_HTTP_MODULE,           /* module type */
    NULL,                      /* init master */
    NULL,                      /* init module */
    ngx_http_wasm_init_process, /* init process */
    NULL,                      /* init thread */
    NULL,                      /* exit thread */
    ngx_http_wasm_exit_process, /* exit process */
    NULL,                      /* exit master */
    NGX_MODULE_V1_PADDING};

static void *ngx_http_wasm_create_conf(ngx_conf_t *cf) {
    ngx_http_wasm_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_wasm_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->set = NGX_CONF_UNSET;

    return conf;
}

static char *ngx_http_wasm_merge_conf(ngx_conf_t *cf,
                                      ngx_http_wasm_conf_t *parent,
                                      ngx_http_wasm_conf_t *child) {
    (void)cf;

    ngx_conf_merge_value(child->set, parent->set, 0);

    if (child->module_path.data == NULL && parent->module_path.data != NULL) {
        child->module_path = parent->module_path;
    }

    if (child->export_name.data == NULL && parent->export_name.data != NULL) {
        child->export_name = parent->export_name;
    }

    return NGX_CONF_OK;
}

static void *ngx_http_wasm_create_main_conf(ngx_conf_t *cf) {
    return ngx_http_wasm_create_conf(cf);
}

static char *ngx_http_wasm_init_main_conf(ngx_conf_t *cf, void *conf) {
    ngx_http_wasm_conf_t *wmcf = conf;

    if (wmcf->set == NGX_CONF_UNSET) {
        wmcf->set = 0;
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

static ngx_int_t ngx_http_wasm_init_process(ngx_cycle_t *cycle) {
    return ngx_http_wasm_runtime_init_process(cycle);
}

static void ngx_http_wasm_exit_process(ngx_cycle_t *cycle) {
    ngx_http_wasm_runtime_exit_process(cycle);
}

static char *
ngx_http_wasm_content_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_conf_t *wcf;
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

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_wasm_content_handler(ngx_http_request_t *r) {
    ngx_http_wasm_conf_t *wcf;
    ngx_http_wasm_exec_ctx_t exec;
    ngx_int_t rc;

    wcf = ngx_http_get_module_loc_conf(r, ngx_http_wasm_module);

    if (wcf->set != 1) {
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

    ngx_http_wasm_runtime_init_exec_ctx(&exec, r, wcf);

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
