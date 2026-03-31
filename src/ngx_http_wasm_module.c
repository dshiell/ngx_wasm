#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
    ngx_flag_t set;
    ngx_str_t module_path;
    ngx_str_t export_name;
} ngx_http_wasm_conf_t;

static ngx_int_t ngx_http_wasm_content_handler(ngx_http_request_t *r);
static char *
ngx_http_wasm_content_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_wasm_install_content_handler(ngx_conf_t *cf);
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
     0,
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
    NULL,                      /* init process */
    NULL,                      /* init thread */
    NULL,                      /* exit thread */
    NULL,                      /* exit process */
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

    if (conf->set) {
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

static char *
ngx_http_wasm_content_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_conf_t *wcf;
    ngx_str_t *value;

    (void)cmd;
    (void)conf;

    if (cf->cmd_type & NGX_HTTP_MAIN_CONF) {
        wcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_wasm_module);

    } else if (cf->cmd_type & NGX_HTTP_SRV_CONF) {
        wcf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_wasm_module);

    } else {
        wcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_wasm_module);
    }

    if (wcf->set) {
        return "is duplicate";
    }

    value = cf->args->elts;

    wcf->module_path = value[1];
    wcf->export_name = value[2];
    wcf->set = 1;

    if (ngx_conf_full_name(cf->cycle, &wcf->module_path, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    if (cf->cmd_type & NGX_HTTP_LOC_CONF) {
        if (ngx_http_wasm_install_content_handler(cf) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_wasm_stub_send_response(ngx_http_request_t *r,
                                                  ngx_str_t *body) {
    ngx_buf_t *b;
    ngx_chain_t out;

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = body->len;
    ngx_str_set(&r->headers_out.content_type, "text/plain");

    if (ngx_http_send_header(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->pos = body->data;
    b->last = body->data + body->len;
    b->memory = 1;
    b->last_buf = 1;

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}

static ngx_int_t ngx_http_wasm_content_handler(ngx_http_request_t *r) {
    ngx_http_wasm_conf_t *wcf;
    ngx_str_t body;
    ngx_int_t rc;

    wcf = ngx_http_get_module_loc_conf(r, ngx_http_wasm_module);

    if (!wcf->set) {
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

    ngx_str_set(&body, "ngx_wasm phase 1 stub\n");

    rc = ngx_http_wasm_stub_send_response(r, &body);
    if (rc == NGX_ERROR) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    return rc;
}
