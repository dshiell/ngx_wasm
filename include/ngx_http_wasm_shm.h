#ifndef _NGX_HTTP_WASM_SHM_H_INCLUDED_
#define _NGX_HTTP_WASM_SHM_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#define NGX_HTTP_WASM_SHM_MAX_KEY_LEN 256
#define NGX_HTTP_WASM_SHM_MAX_VALUE_LEN 65535

typedef struct ngx_http_wasm_shm_zone_s ngx_http_wasm_shm_zone_t;

struct ngx_http_wasm_shm_zone_s {
    ngx_shm_zone_t *zone;
    ngx_slab_pool_t *shpool;
    void *sh;
    ngx_str_t name;
    size_t size;
};

char *ngx_http_wasm_shm_add_zone(ngx_conf_t *cf,
                                 ngx_http_wasm_shm_zone_t **dst,
                                 ngx_str_t *name,
                                 size_t size);
ngx_int_t ngx_http_wasm_shm_get(ngx_http_wasm_shm_zone_t *zone,
                                const u_char *key,
                                size_t key_len,
                                u_char *buf,
                                size_t buf_len);
ngx_int_t ngx_http_wasm_shm_exists(ngx_http_wasm_shm_zone_t *zone,
                                   const u_char *key,
                                   size_t key_len);
ngx_int_t ngx_http_wasm_shm_incr(ngx_http_wasm_shm_zone_t *zone,
                                 const u_char *key,
                                 size_t key_len,
                                 ngx_int_t delta);
ngx_int_t ngx_http_wasm_shm_set(ngx_http_wasm_shm_zone_t *zone,
                                const u_char *key,
                                size_t key_len,
                                const u_char *value,
                                size_t value_len);
ngx_int_t ngx_http_wasm_shm_add(ngx_http_wasm_shm_zone_t *zone,
                                const u_char *key,
                                size_t key_len,
                                const u_char *value,
                                size_t value_len);
ngx_int_t ngx_http_wasm_shm_replace(ngx_http_wasm_shm_zone_t *zone,
                                    const u_char *key,
                                    size_t key_len,
                                    const u_char *value,
                                    size_t value_len);
ngx_int_t ngx_http_wasm_shm_delete(ngx_http_wasm_shm_zone_t *zone,
                                   const u_char *key,
                                   size_t key_len);

#endif /* _NGX_HTTP_WASM_SHM_H_INCLUDED_ */
