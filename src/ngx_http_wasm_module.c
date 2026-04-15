#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#if (NGX_HTTP_SSL)
#include <ngx_event_openssl.h>
#include <ngx_http_ssl_module.h>
#endif

#include <ngx_http_wasm_module_int.h>
#include <ngx_http_wasm_metrics.h>
#include <ngx_http_wasm_runtime.h>
#include <ngx_http_wasm_shm.h>

static ngx_int_t ngx_http_wasm_content_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_wasm_metrics_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_wasm_server_rewrite_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_wasm_rewrite_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_wasm_log_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_wasm_run_request(ngx_http_request_t *r,
                                           ngx_http_wasm_ctx_t *ctx);
static void
ngx_http_wasm_init_request_exec(ngx_http_wasm_ctx_t *ctx,
                                ngx_http_request_t *r,
                                ngx_http_wasm_phase_conf_t *phase,
                                ngx_http_wasm_phase_e phase_kind,
                                ngx_http_wasm_runtime_state_t *runtime,
                                ngx_uint_t fuel_limit,
                                ngx_uint_t timeslice_fuel);
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
ngx_http_wasm_body_filter_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *
ngx_http_wasm_log_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_wasm_ssl_client_hello_by(ngx_conf_t *cf,
                                               ngx_command_t *cmd,
                                               void *conf);
static char *ngx_http_wasm_ssl_certificate_by(ngx_conf_t *cf,
                                              ngx_command_t *cmd,
                                              void *conf);
static char *
ngx_http_wasm_rewrite_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_wasm_request_body_buffer_size(ngx_conf_t *cf,
                                                    ngx_command_t *cmd,
                                                    void *conf);
static char *ngx_http_wasm_subrequest_buffer_size(ngx_conf_t *cf,
                                                  ngx_command_t *cmd,
                                                  void *conf);
static char *
ngx_http_wasm_shm_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *
ngx_http_wasm_metrics_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *
ngx_http_wasm_counter(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *
ngx_http_wasm_gauge(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *
ngx_http_wasm_metrics(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_wasm_body_filter_file_chunk_size(ngx_conf_t *cf,
                                                       ngx_command_t *cmd,
                                                       void *conf);
static char *
ngx_http_wasm_fuel_limit(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *
ngx_http_wasm_timeslice_fuel(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_wasm_install_content_handler(ngx_conf_t *cf);
static ngx_int_t ngx_http_wasm_install_metrics_handler(ngx_conf_t *cf);
static ngx_int_t ngx_http_wasm_postconfiguration(ngx_conf_t *cf);
#if (NGX_HTTP_SSL)
static ngx_int_t ngx_http_wasm_ssl_init(ngx_conf_t *cf);
static int
ngx_http_wasm_ssl_servername(ngx_ssl_conn_t *ssl_conn, int *ad, void *arg);
static int ngx_http_wasm_ssl_certificate(ngx_ssl_conn_t *ssl_conn, void *arg);
#endif
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

    {ngx_string("body_filter_by_wasm"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE2,
     ngx_http_wasm_body_filter_by,
     NGX_HTTP_LOC_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("log_by_wasm"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE2,
     ngx_http_wasm_log_by,
     NGX_HTTP_LOC_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("ssl_client_hello_by_wasm"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_CONF_TAKE2,
     ngx_http_wasm_ssl_client_hello_by,
     NGX_HTTP_SRV_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("ssl_certificate_by_wasm"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_CONF_TAKE2,
     ngx_http_wasm_ssl_certificate_by,
     NGX_HTTP_SRV_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("wasm_fuel_limit"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE1,
     ngx_http_wasm_fuel_limit,
     NGX_HTTP_LOC_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("wasm_shm_zone"),
     NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE2,
     ngx_http_wasm_shm_zone,
     NGX_HTTP_MAIN_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("wasm_metrics_zone"),
     NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE2,
     ngx_http_wasm_metrics_zone,
     NGX_HTTP_MAIN_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("wasm_counter"),
     NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
     ngx_http_wasm_counter,
     NGX_HTTP_MAIN_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("wasm_gauge"),
     NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
     ngx_http_wasm_gauge,
     NGX_HTTP_MAIN_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("wasm_metrics"),
     NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
     ngx_http_wasm_metrics,
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

    {ngx_string("wasm_body_filter_file_chunk_size"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE1,
     ngx_http_wasm_body_filter_file_chunk_size,
     NGX_HTTP_LOC_CONF_OFFSET,
     0,
     NULL},

    {ngx_string("wasm_subrequest_buffer_size"),
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE1,
     ngx_http_wasm_subrequest_buffer_size,
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
    conf->body_filter.set = NGX_CONF_UNSET;
    conf->log.set = NGX_CONF_UNSET;
    conf->ssl_client_hello.set = NGX_CONF_UNSET;
    conf->ssl_certificate.set = NGX_CONF_UNSET;
    conf->fuel_limit = NGX_CONF_UNSET_UINT;
    conf->timeslice_fuel = NGX_CONF_UNSET_UINT;
    conf->request_body_buffer_size = NGX_CONF_UNSET_SIZE;
    conf->body_filter_file_chunk_size = NGX_CONF_UNSET_SIZE;
    conf->subrequest_buffer_size = NGX_CONF_UNSET_SIZE;
    conf->metrics_endpoint = NGX_CONF_UNSET;

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
    ngx_conf_merge_size_value(
        child->body_filter_file_chunk_size,
        parent->body_filter_file_chunk_size,
        NGX_HTTP_WASM_DEFAULT_BODY_FILTER_FILE_CHUNK_SIZE);
    ngx_conf_merge_size_value(child->subrequest_buffer_size,
                              parent->subrequest_buffer_size,
                              NGX_HTTP_WASM_DEFAULT_SUBREQUEST_BUFFER_SIZE);
    ngx_conf_merge_value(child->metrics_endpoint, parent->metrics_endpoint, 0);

    ngx_http_wasm_merge_phase_conf(&parent->content, &child->content);
    ngx_http_wasm_merge_phase_conf(&parent->rewrite, &child->rewrite);
    ngx_http_wasm_merge_phase_conf(&parent->server_rewrite,
                                   &child->server_rewrite);
    ngx_http_wasm_merge_phase_conf(&parent->header_filter,
                                   &child->header_filter);
    ngx_http_wasm_merge_phase_conf(&parent->body_filter, &child->body_filter);
    ngx_http_wasm_merge_phase_conf(&parent->log, &child->log);
    ngx_http_wasm_merge_phase_conf(&parent->ssl_client_hello,
                                   &child->ssl_client_hello);
    ngx_http_wasm_merge_phase_conf(&parent->ssl_certificate,
                                   &child->ssl_certificate);

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

    wmcf->metric_definitions =
        ngx_array_create(cf->pool, 4, sizeof(ngx_http_wasm_metric_def_t));
    if (wmcf->metric_definitions == NULL) {
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

    if (wmcf->modules == NULL || wmcf->runtime == NULL ||
        wmcf->metric_definitions == NULL) {
        return NGX_CONF_ERROR;
    }

    if (wmcf->metric_definitions->nelts != 0 && wmcf->metrics_zone == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG,
                           cf,
                           0,
                           "\"wasm_metrics_zone\" is required when metrics "
                           "are declared");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_wasm_shm_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_str_t *value;
    off_t size;

    (void)cmd;
    wmcf = conf;

    value = cf->args->elts;
    size = ngx_parse_size(&value[2]);
    if (size == NGX_ERROR || size < (off_t)(8 * ngx_pagesize)) {
        ngx_conf_log_error(NGX_LOG_EMERG,
                           cf,
                           0,
                           "invalid wasm_shm_zone size \"%V\"",
                           &value[2]);
        return NGX_CONF_ERROR;
    }

    return ngx_http_wasm_shm_add_zone(
        cf, &wmcf->shm_zone, &value[1], (size_t)size);
}

static char *
ngx_http_wasm_metrics_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_str_t *value;
    off_t size;

    (void)cmd;
    wmcf = conf;

    value = cf->args->elts;
    size = ngx_parse_size(&value[2]);
    if (size == NGX_ERROR || size < (off_t)(8 * ngx_pagesize)) {
        ngx_conf_log_error(NGX_LOG_EMERG,
                           cf,
                           0,
                           "invalid wasm_metrics_zone size \"%V\"",
                           &value[2]);
        return NGX_CONF_ERROR;
    }

    return ngx_http_wasm_metrics_add_zone(cf,
                                          &wmcf->metrics_zone,
                                          wmcf->metric_definitions,
                                          &value[1],
                                          (size_t)size);
}

static char *
ngx_http_wasm_counter(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_str_t *value;

    (void)cmd;
    wmcf = conf;
    value = cf->args->elts;

    return ngx_http_wasm_metrics_declare(cf,
                                         wmcf->metric_definitions,
                                         &value[1],
                                         NGX_HTTP_WASM_METRIC_KIND_COUNTER);
}

static char *
ngx_http_wasm_gauge(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_str_t *value;

    (void)cmd;
    wmcf = conf;
    value = cf->args->elts;

    return ngx_http_wasm_metrics_declare(cf,
                                         wmcf->metric_definitions,
                                         &value[1],
                                         NGX_HTTP_WASM_METRIC_KIND_GAUGE);
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
        if (conf->metrics_endpoint == 1) {
            ngx_conf_log_error(NGX_LOG_EMERG,
                               cf,
                               0,
                               "\"content_by_wasm\" conflicts with "
                               "\"wasm_metrics\"");
            return NGX_CONF_ERROR;
        }

        if (ngx_http_wasm_install_content_handler(cf) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    if (conf->metrics_endpoint == 1) {
        if (ngx_http_wasm_install_metrics_handler(cf) != NGX_OK) {
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

static ngx_int_t ngx_http_wasm_install_metrics_handler(ngx_conf_t *cf) {
    ngx_http_core_loc_conf_t *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_wasm_metrics_handler;

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

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_wasm_log_handler;

#if (NGX_HTTP_SSL)
    if (ngx_http_wasm_ssl_init(cf) != NGX_OK) {
        return NGX_ERROR;
    }
#endif

    return NGX_OK;
}

static ngx_int_t ngx_http_wasm_init_module(ngx_cycle_t *cycle) {
    (void)cycle;

    return NGX_OK;
}

static ngx_int_t ngx_http_wasm_init_process(ngx_cycle_t *cycle) {
    ngx_int_t rc;

    (void)cycle;

    rc = ngx_http_wasm_header_filter_init_process();
    if (rc != NGX_OK) {
        return rc;
    }

    return ngx_http_wasm_body_filter_init_process();
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

static char *
ngx_http_wasm_metrics(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_conf_t *wcf;

    (void)cmd;
    wcf = conf;

    if (wcf->metrics_endpoint == 1) {
        return "is duplicate";
    }

    wcf->metrics_endpoint = 1;

    return NGX_CONF_OK;
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
ngx_http_wasm_body_filter_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_conf_t *wcf;

    (void)cmd;
    wcf = conf;

    switch (ngx_http_wasm_configure_phase(cf, &wcf->body_filter)) {
        case NGX_OK:
            return NGX_CONF_OK;
        case NGX_DECLINED:
            return "is duplicate";
        default:
            return NGX_CONF_ERROR;
    }
}

static char *
ngx_http_wasm_log_by(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_wasm_conf_t *wcf;

    (void)cmd;
    wcf = conf;

    switch (ngx_http_wasm_configure_phase(cf, &wcf->log)) {
        case NGX_OK:
            return NGX_CONF_OK;
        case NGX_DECLINED:
            return "is duplicate";
        default:
            return NGX_CONF_ERROR;
    }
}

static char *ngx_http_wasm_ssl_client_hello_by(ngx_conf_t *cf,
                                               ngx_command_t *cmd,
                                               void *conf) {
    ngx_http_wasm_conf_t *wcf;

    (void)cmd;
    wcf = conf;

    switch (ngx_http_wasm_configure_phase(cf, &wcf->ssl_client_hello)) {
        case NGX_OK:
            return NGX_CONF_OK;
        case NGX_DECLINED:
            return "is duplicate";
        default:
            return NGX_CONF_ERROR;
    }
}

static char *ngx_http_wasm_ssl_certificate_by(ngx_conf_t *cf,
                                              ngx_command_t *cmd,
                                              void *conf) {
    ngx_http_wasm_conf_t *wcf;

    (void)cmd;
    wcf = conf;

    switch (ngx_http_wasm_configure_phase(cf, &wcf->ssl_certificate)) {
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

static char *ngx_http_wasm_body_filter_file_chunk_size(ngx_conf_t *cf,
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
        ngx_conf_log_error(
            NGX_LOG_EMERG,
            cf,
            0,
            "invalid wasm_body_filter_file_chunk_size value \"%V\"",
            &value[1]);
        return NGX_CONF_ERROR;
    }

    wcf->body_filter_file_chunk_size = (size_t)size;

    return NGX_CONF_OK;
}

static char *ngx_http_wasm_subrequest_buffer_size(ngx_conf_t *cf,
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
                           "invalid wasm_subrequest_buffer_size value \"%V\"",
                           &value[1]);
        return NGX_CONF_ERROR;
    }

    wcf->subrequest_buffer_size = (size_t)size;

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

static ngx_int_t ngx_http_wasm_metrics_handler(ngx_http_request_t *r) {
    ngx_buf_t *b;
    ngx_chain_t out;
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_http_wasm_metric_def_t *defs;
    ngx_int_t rc;
    ngx_uint_t i;
    ngx_uint_t kind;
    int64_t value;
    size_t len;
    u_char *p, *last;

    wmcf = ngx_http_get_module_main_conf(r, ngx_http_wasm_module);
    if (wmcf == NULL || wmcf->metrics_zone == NULL ||
        wmcf->metric_definitions == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    defs = wmcf->metric_definitions->elts;
    len = 0;
    for (i = 0; i < wmcf->metric_definitions->nelts; i++) {
        len += sizeof("# TYPE ") - 1 + defs[i].name.len + sizeof(" counter\n") -
               1 + defs[i].name.len + NGX_INT64_LEN + 2;
    }

    p = ngx_pnalloc(r->pool, len == 0 ? 1 : len);
    if (p == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    last = p;
    for (i = 0; i < wmcf->metric_definitions->nelts; i++) {
        rc = ngx_http_wasm_metrics_get(wmcf->metrics_zone,
                                       defs[i].name.data,
                                       defs[i].name.len,
                                       &kind,
                                       &value);
        if (rc != NGX_HTTP_WASM_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        last = ngx_sprintf(
            last,
            "# TYPE %V %s\n%V %L\n",
            &defs[i].name,
            (kind == NGX_HTTP_WASM_METRIC_KIND_COUNTER) ? "counter" : "gauge",
            &defs[i].name,
            value);
    }

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = last - p;
    ngx_str_set(&r->headers_out.content_type, "text/plain; version=0.0.4");

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->pos = p;
    b->last = last;
    b->memory = 1;
    b->last_buf = 1;

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
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

static ngx_int_t ngx_http_wasm_log_handler(ngx_http_request_t *r) {
    static ngx_str_t phase_name = ngx_string("log");
    ngx_http_wasm_conf_t *wcf;
    ngx_http_wasm_ctx_t *ctx;
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_int_t rc;

    if (r != r->main) {
        return NGX_OK;
    }

    wcf = ngx_http_get_module_loc_conf(r, ngx_http_wasm_module);
    wmcf = ngx_http_get_module_main_conf(r, ngx_http_wasm_module);

    if (wcf == NULL || wcf->log.set != 1 || wmcf == NULL ||
        wmcf->runtime == NULL) {
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_NOTICE,
                  r->connection->log,
                  0,
                  "ngx_wasm: %V handler module=\"%V\" export=\"%V\"",
                  &phase_name,
                  &wcf->log.module_path,
                  &wcf->log.export_name);

    ctx = ngx_http_wasm_get_or_create_ctx(r, wcf, wmcf, &wcf->log);
    if (ctx == NULL || !ctx->log_exec_set) {
        ngx_log_error(NGX_LOG_ERR,
                      r->connection->log,
                      0,
                      "ngx_wasm: failed to initialize log phase context");
        return NGX_OK;
    }

    rc = ngx_http_wasm_runtime_run(&ctx->log_exec);
    if (rc == NGX_AGAIN) {
        ngx_log_error(NGX_LOG_ERR,
                      r->connection->log,
                      0,
                      "ngx_wasm: log_by_wasm does not support suspension");
    }

    ngx_http_wasm_runtime_cleanup_exec_ctx(&ctx->log_exec);
    ctx->log_exec_set = 0;

    return NGX_OK;
}

#if (NGX_HTTP_SSL)
static ngx_int_t ngx_http_wasm_ssl_init(ngx_conf_t *cf) {
    static ngx_ssl_client_hello_arg cb = {ngx_http_wasm_ssl_servername};
    ngx_http_core_main_conf_t *cmcf;
    ngx_http_core_srv_conf_t **cscfp;
    ngx_http_ssl_srv_conf_t *sscf;
    ngx_http_wasm_conf_t *wcf;
    ngx_uint_t s;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    cscfp = cmcf->servers.elts;

    for (s = 0; s < cmcf->servers.nelts; s++) {
        wcf = cscfp[s]->ctx->srv_conf[ngx_http_wasm_module.ctx_index];
        sscf = cscfp[s]->ctx->srv_conf[ngx_http_ssl_module.ctx_index];

        if (wcf == NULL || sscf == NULL || sscf->ssl.ctx == NULL) {
            continue;
        }

        if (wcf->ssl_client_hello.set == 1) {
            if (ngx_ssl_set_client_hello_callback(&sscf->ssl, &cb) != NGX_OK) {
                return NGX_ERROR;
            }

            if (SSL_CTX_set_tlsext_servername_callback(
                    sscf->ssl.ctx, ngx_http_wasm_ssl_servername) == 0) {
                ngx_conf_log_error(
                    NGX_LOG_WARN,
                    cf,
                    0,
                    "nginx was built with SNI support, but the linked OpenSSL "
                    "library has no tlsext support");
            }
        }

        if (wcf->ssl_certificate.set == 1) {
#ifdef SSL_R_CERT_CB_ERROR
            SSL_CTX_set_cert_cb(
                sscf->ssl.ctx, ngx_http_wasm_ssl_certificate, NULL);
#else
            ngx_conf_log_error(NGX_LOG_EMERG,
                               cf,
                               0,
                               "\"ssl_certificate_by_wasm\" is not supported "
                               "on this platform");
            return NGX_ERROR;
#endif
        }
    }

    return NGX_OK;
}

static int
ngx_http_wasm_ssl_servername(ngx_ssl_conn_t *ssl_conn, int *ad, void *arg) {
    ngx_connection_t *c;
    ngx_http_connection_t *hc;
    ngx_http_wasm_conf_t *wcf;
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_http_wasm_exec_ctx_t exec;
    ngx_int_t rc;

    rc = ngx_http_ssl_servername(ssl_conn, ad, arg);
    if (rc != SSL_TLSEXT_ERR_OK) {
        return rc;
    }

    c = ngx_ssl_get_connection(ssl_conn);
    hc = c->data;
    if (hc == NULL || hc->conf_ctx == NULL) {
        return SSL_TLSEXT_ERR_OK;
    }

    wcf = ngx_http_get_module_srv_conf(hc->conf_ctx, ngx_http_wasm_module);
    wmcf = ngx_http_get_module_main_conf(hc->conf_ctx, ngx_http_wasm_module);
    if (wcf == NULL || wmcf == NULL || wmcf->runtime == NULL ||
        wcf->ssl_client_hello.set != 1) {
        return SSL_TLSEXT_ERR_OK;
    }

    ngx_log_error(
        NGX_LOG_NOTICE,
        c->log,
        0,
        "ngx_wasm: ssl client hello handler module=\"%V\" export=\"%V\"",
        &wcf->ssl_client_hello.module_path,
        &wcf->ssl_client_hello.export_name);

    ngx_http_wasm_runtime_init_ssl_exec_ctx(
        &exec,
        c,
        ssl_conn,
        &wcf->ssl_client_hello,
        NGX_HTTP_WASM_PHASE_SSL_CLIENT_HELLO,
        wmcf->runtime);
    exec.fuel_limit = wcf->fuel_limit;
    exec.timeslice_fuel = 0;
    exec.fuel_remaining = wcf->fuel_limit;

    rc = ngx_http_wasm_runtime_run(&exec);
    ngx_http_wasm_runtime_cleanup_exec_ctx(&exec);

    if (rc != NGX_OK) {
        *ad = SSL_AD_INTERNAL_ERROR;
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    if (exec.abi.ssl_handshake_rejected) {
        c->ssl->handshake_rejected = 1;
        *ad = exec.abi.ssl_handshake_alert;
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    return SSL_TLSEXT_ERR_OK;
}

static int ngx_http_wasm_ssl_certificate(ngx_ssl_conn_t *ssl_conn, void *arg) {
    ngx_connection_t *c;
    ngx_http_connection_t *hc;
    ngx_http_wasm_conf_t *wcf;
    ngx_http_wasm_main_conf_t *wmcf;
    ngx_http_wasm_exec_ctx_t exec;
    ngx_int_t rc;

    (void)arg;

    c = ngx_ssl_get_connection(ssl_conn);
    hc = c->data;
    if (hc == NULL || hc->conf_ctx == NULL) {
        return 0;
    }

    wcf = ngx_http_get_module_srv_conf(hc->conf_ctx, ngx_http_wasm_module);
    wmcf = ngx_http_get_module_main_conf(hc->conf_ctx, ngx_http_wasm_module);
    if (wcf == NULL || wmcf == NULL || wmcf->runtime == NULL ||
        wcf->ssl_certificate.set != 1) {
        return 0;
    }

    ngx_log_error(
        NGX_LOG_NOTICE,
        c->log,
        0,
        "ngx_wasm: ssl certificate handler module=\"%V\" export=\"%V\"",
        &wcf->ssl_certificate.module_path,
        &wcf->ssl_certificate.export_name);

    ngx_http_wasm_runtime_init_ssl_exec_ctx(&exec,
                                            c,
                                            ssl_conn,
                                            &wcf->ssl_certificate,
                                            NGX_HTTP_WASM_PHASE_SSL_CERTIFICATE,
                                            wmcf->runtime);
    exec.fuel_limit = wcf->fuel_limit;
    exec.timeslice_fuel = 0;
    exec.fuel_remaining = wcf->fuel_limit;

    rc = ngx_http_wasm_runtime_run(&exec);
    ngx_http_wasm_runtime_cleanup_exec_ctx(&exec);

    if (rc != NGX_OK) {
        return 0;
    }

    if (exec.abi.ssl_handshake_rejected) {
        c->ssl->handshake_rejected = 1;
        return 0;
    }

    return 1;
}
#endif

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

    if (ctx->exec_completed &&
        (phase == &wcf->rewrite || phase == &wcf->server_rewrite)) {
        return NGX_DECLINED;
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
        if (phase == &wcf->log && wcf->log.set == 1 && !ctx->log_exec_set) {
            ngx_http_wasm_runtime_init_exec_ctx(&ctx->log_exec,
                                                r,
                                                &wcf->log,
                                                NGX_HTTP_WASM_PHASE_LOG,
                                                wmcf->runtime);
            ctx->log_exec.fuel_limit = wcf->fuel_limit;
            ctx->log_exec.timeslice_fuel = 0;
            ctx->log_exec.fuel_remaining = wcf->fuel_limit;
            ctx->log_exec_set = 1;
        }

        if (ctx->exec_completed && ctx->exec.conf != phase) {
            if (phase == &wcf->rewrite && wcf->rewrite.set == 1) {
                ngx_http_wasm_init_request_exec(ctx,
                                                r,
                                                &wcf->rewrite,
                                                NGX_HTTP_WASM_PHASE_REWRITE,
                                                wmcf->runtime,
                                                wcf->fuel_limit,
                                                wcf->timeslice_fuel);
            } else if (phase == &wcf->server_rewrite &&
                       wcf->server_rewrite.set == 1) {
                ngx_http_wasm_init_request_exec(
                    ctx,
                    r,
                    &wcf->server_rewrite,
                    NGX_HTTP_WASM_PHASE_SERVER_REWRITE,
                    wmcf->runtime,
                    wcf->fuel_limit,
                    wcf->timeslice_fuel);
            } else if (phase == &wcf->content && wcf->content.set == 1) {
                ngx_http_wasm_init_request_exec(ctx,
                                                r,
                                                &wcf->content,
                                                NGX_HTTP_WASM_PHASE_CONTENT,
                                                wmcf->runtime,
                                                wcf->fuel_limit,
                                                wcf->timeslice_fuel);
            }
        }

        return ctx;
    }

    ctx = ngx_pcalloc(r->pool, sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }

    ctx->subrequest.request = r;

    ngx_http_set_ctx(r, ctx, ngx_http_wasm_module);

    cln = ngx_pool_cleanup_add(r->pool, 0);
    if (cln == NULL) {
        return NULL;
    }

    cln->handler = ngx_http_wasm_cleanup_ctx;
    cln->data = ctx;

    if (phase == &wcf->rewrite && wcf->rewrite.set == 1) {
        ngx_http_wasm_init_request_exec(ctx,
                                        r,
                                        &wcf->rewrite,
                                        NGX_HTTP_WASM_PHASE_REWRITE,
                                        wmcf->runtime,
                                        wcf->fuel_limit,
                                        wcf->timeslice_fuel);
        return ctx;
    }

    if (phase == &wcf->server_rewrite && wcf->server_rewrite.set == 1) {
        ngx_http_wasm_init_request_exec(ctx,
                                        r,
                                        &wcf->server_rewrite,
                                        NGX_HTTP_WASM_PHASE_SERVER_REWRITE,
                                        wmcf->runtime,
                                        wcf->fuel_limit,
                                        wcf->timeslice_fuel);
        return ctx;
    }

    if (phase == &wcf->content && wcf->content.set == 1) {
        ngx_http_wasm_init_request_exec(ctx,
                                        r,
                                        &wcf->content,
                                        NGX_HTTP_WASM_PHASE_CONTENT,
                                        wmcf->runtime,
                                        wcf->fuel_limit,
                                        wcf->timeslice_fuel);
        return ctx;
    }

    if (phase == &wcf->log && wcf->log.set == 1) {
        ngx_http_wasm_runtime_init_exec_ctx(&ctx->log_exec,
                                            r,
                                            &wcf->log,
                                            NGX_HTTP_WASM_PHASE_LOG,
                                            wmcf->runtime);
        ctx->log_exec.fuel_limit = wcf->fuel_limit;
        ctx->log_exec.timeslice_fuel = 0;
        ctx->log_exec.fuel_remaining = wcf->fuel_limit;
        ctx->log_exec_set = 1;
        return ctx;
    }

    return NULL;
}

static void
ngx_http_wasm_init_request_exec(ngx_http_wasm_ctx_t *ctx,
                                ngx_http_request_t *r,
                                ngx_http_wasm_phase_conf_t *phase,
                                ngx_http_wasm_phase_e phase_kind,
                                ngx_http_wasm_runtime_state_t *runtime,
                                ngx_uint_t fuel_limit,
                                ngx_uint_t timeslice_fuel) {
    ngx_http_wasm_runtime_cleanup_exec_ctx(&ctx->exec);
    ngx_http_wasm_runtime_init_exec_ctx(
        &ctx->exec, r, phase, phase_kind, runtime);
    ctx->exec.fuel_limit = fuel_limit;
    ctx->exec.timeslice_fuel = timeslice_fuel;
    ctx->exec.fuel_remaining = fuel_limit;
    ctx->exec_completed = 0;
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

        if (ctx->exec.suspend_kind == NGX_HTTP_WASM_SUSPEND_RESCHEDULE) {
            if (ngx_http_post_request(r, NULL) != NGX_OK) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
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
    ctx->exec_completed = 1;

    if ((ctx->exec.phase_kind == NGX_HTTP_WASM_PHASE_REWRITE ||
         ctx->exec.phase_kind == NGX_HTTP_WASM_PHASE_SERVER_REWRITE) &&
        !ctx->exec.abi.status_set && !ctx->exec.abi.body_set &&
        !ctx->exec.abi.content_type_set) {
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

    if (ctx->body_filter_exec_set) {
        ngx_http_wasm_runtime_cleanup_exec_ctx(&ctx->body_filter_exec);
    }
}

static void ngx_http_wasm_cleanup_main_conf(void *data) {
    ngx_http_wasm_main_conf_t *wmcf = data;

    ngx_http_wasm_runtime_destroy(wmcf);
}
