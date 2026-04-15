#ifndef _NGX_HTTP_WASM_METRICS_H_INCLUDED_
#define _NGX_HTTP_WASM_METRICS_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#define NGX_HTTP_WASM_METRIC_MAX_NAME_LEN 256
#define NGX_HTTP_WASM_METRIC_KIND_COUNTER 1
#define NGX_HTTP_WASM_METRIC_KIND_GAUGE 2
#define NGX_HTTP_WASM_LOG_MAX_MESSAGE_LEN 1024

typedef struct ngx_http_wasm_metrics_zone_s ngx_http_wasm_metrics_zone_t;

typedef struct {
    ngx_str_t name;
    ngx_uint_t kind;
} ngx_http_wasm_metric_def_t;

struct ngx_http_wasm_metrics_zone_s {
    ngx_shm_zone_t *zone;
    ngx_slab_pool_t *shpool;
    void *sh;
    ngx_str_t name;
    size_t size;
    ngx_array_t *definitions;
};

char *ngx_http_wasm_metrics_add_zone(ngx_conf_t *cf,
                                     ngx_http_wasm_metrics_zone_t **dst,
                                     ngx_array_t *definitions,
                                     ngx_str_t *name,
                                     size_t size);
char *ngx_http_wasm_metrics_declare(ngx_conf_t *cf,
                                    ngx_array_t *definitions,
                                    ngx_str_t *name,
                                    ngx_uint_t kind);
ngx_int_t ngx_http_wasm_metrics_counter_inc(ngx_http_wasm_metrics_zone_t *zone,
                                            const u_char *name,
                                            size_t name_len,
                                            ngx_int_t delta);
ngx_int_t ngx_http_wasm_metrics_gauge_set(ngx_http_wasm_metrics_zone_t *zone,
                                          const u_char *name,
                                          size_t name_len,
                                          ngx_int_t value);
ngx_int_t ngx_http_wasm_metrics_gauge_add(ngx_http_wasm_metrics_zone_t *zone,
                                          const u_char *name,
                                          size_t name_len,
                                          ngx_int_t delta);
ngx_int_t ngx_http_wasm_metrics_get(ngx_http_wasm_metrics_zone_t *zone,
                                    const u_char *name,
                                    size_t name_len,
                                    ngx_uint_t *kind,
                                    int64_t *value);

#endif /* _NGX_HTTP_WASM_METRICS_H_INCLUDED_ */
