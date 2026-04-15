#include <ngx_http_wasm_abi.h>
#include <ngx_http_wasm_metrics.h>
#include <ngx_http_wasm_module_int.h>

typedef struct {
    ngx_rbtree_t rbtree;
    ngx_rbtree_node_t sentinel;
} ngx_http_wasm_metrics_ctx_t;

typedef struct {
    ngx_rbtree_node_t node;
    size_t name_len;
    ngx_uint_t kind;
    int64_t value;
    u_char data[1];
} ngx_http_wasm_metric_node_t;

static ngx_int_t ngx_http_wasm_metrics_init_zone(ngx_shm_zone_t *shm_zone,
                                                 void *data);
static ngx_int_t
ngx_http_wasm_metrics_ensure_definitions(ngx_http_wasm_metrics_zone_t *zone);
static void
ngx_http_wasm_metrics_rbtree_insert_value(ngx_rbtree_node_t *temp,
                                          ngx_rbtree_node_t *node,
                                          ngx_rbtree_node_t *sentinel);
static ngx_http_wasm_metric_node_t *
ngx_http_wasm_metrics_lookup(ngx_http_wasm_metrics_zone_t *zone,
                             const u_char *name,
                             size_t name_len,
                             uint32_t hash);
static ngx_http_wasm_metric_node_t *
ngx_http_wasm_metrics_alloc_node(ngx_http_wasm_metrics_zone_t *zone,
                                 const u_char *name,
                                 size_t name_len,
                                 ngx_uint_t kind,
                                 uint32_t hash);

char *ngx_http_wasm_metrics_add_zone(ngx_conf_t *cf,
                                     ngx_http_wasm_metrics_zone_t **dst,
                                     ngx_array_t *definitions,
                                     ngx_str_t *name,
                                     size_t size) {
    ngx_http_wasm_metrics_zone_t *zone_ctx;
    ngx_shm_zone_t *shm_zone;

    if (*dst != NULL) {
        return "is duplicate";
    }

    zone_ctx = ngx_pcalloc(cf->pool, sizeof(*zone_ctx));
    if (zone_ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    zone_ctx->name = *name;
    zone_ctx->size = size;
    zone_ctx->definitions = definitions;

    shm_zone = ngx_shared_memory_add(cf, name, size, &ngx_http_wasm_module);
    if (shm_zone == NULL) {
        return NGX_CONF_ERROR;
    }

    if (shm_zone->data != NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG,
                           cf,
                           0,
                           "\"wasm_metrics_zone\" \"%V\" is duplicate",
                           name);
        return NGX_CONF_ERROR;
    }

    shm_zone->init = ngx_http_wasm_metrics_init_zone;
    shm_zone->data = zone_ctx;
    zone_ctx->zone = shm_zone;
    *dst = zone_ctx;

    return NGX_CONF_OK;
}

char *ngx_http_wasm_metrics_declare(ngx_conf_t *cf,
                                    ngx_array_t *definitions,
                                    ngx_str_t *name,
                                    ngx_uint_t kind) {
    ngx_http_wasm_metric_def_t *def;
    ngx_uint_t i;

    if (definitions == NULL || name == NULL || name->len == 0 ||
        name->len > NGX_HTTP_WASM_METRIC_MAX_NAME_LEN) {
        return NGX_CONF_ERROR;
    }

    def = definitions->elts;
    for (i = 0; i < definitions->nelts; i++) {
        if (def[i].name.len == name->len &&
            ngx_strncmp(def[i].name.data, name->data, name->len) == 0) {
            ngx_conf_log_error(
                NGX_LOG_EMERG, cf, 0, "\"%V\" metric is duplicate", name);
            return NGX_CONF_ERROR;
        }
    }

    def = ngx_array_push(definitions);
    if (def == NULL) {
        return NGX_CONF_ERROR;
    }

    def->name = *name;
    def->kind = kind;

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_wasm_metrics_init_zone(ngx_shm_zone_t *shm_zone,
                                                 void *data) {
    ngx_http_wasm_metrics_ctx_t *sh;
    ngx_http_wasm_metrics_zone_t *old_zone, *zone;
    ngx_slab_pool_t *shpool;

    zone = shm_zone->data;
    old_zone = data;
    shpool = (ngx_slab_pool_t *)shm_zone->shm.addr;

    zone->zone = shm_zone;
    zone->shpool = shpool;

    if (old_zone != NULL) {
        zone->sh = old_zone->sh;
        return ngx_http_wasm_metrics_ensure_definitions(zone);
    }

    if (shpool->data != NULL) {
        zone->sh = shpool->data;
        return ngx_http_wasm_metrics_ensure_definitions(zone);
    }

    ngx_shmtx_lock(&shpool->mutex);

    sh = ngx_slab_alloc_locked(shpool, sizeof(*sh));
    if (sh == NULL) {
        ngx_shmtx_unlock(&shpool->mutex);
        return NGX_ERROR;
    }

    ngx_rbtree_init(
        &sh->rbtree, &sh->sentinel, ngx_http_wasm_metrics_rbtree_insert_value);

    shpool->data = sh;
    zone->sh = sh;

    ngx_shmtx_unlock(&shpool->mutex);

    return ngx_http_wasm_metrics_ensure_definitions(zone);
}

static ngx_int_t
ngx_http_wasm_metrics_ensure_definitions(ngx_http_wasm_metrics_zone_t *zone) {
    ngx_http_wasm_metric_def_t *defs;
    ngx_http_wasm_metric_node_t *node;
    ngx_http_wasm_metrics_ctx_t *sh;
    ngx_uint_t i;
    uint32_t hash;

    if (zone == NULL || zone->shpool == NULL || zone->sh == NULL ||
        zone->definitions == NULL) {
        return NGX_OK;
    }

    defs = zone->definitions->elts;
    sh = zone->sh;

    ngx_shmtx_lock(&zone->shpool->mutex);

    for (i = 0; i < zone->definitions->nelts; i++) {
        hash = ngx_crc32_short(defs[i].name.data, defs[i].name.len);
        node = ngx_http_wasm_metrics_lookup(
            zone, defs[i].name.data, defs[i].name.len, hash);
        if (node != NULL) {
            if (node->kind != defs[i].kind) {
                ngx_shmtx_unlock(&zone->shpool->mutex);
                return NGX_ERROR;
            }

            continue;
        }

        node = ngx_http_wasm_metrics_alloc_node(
            zone, defs[i].name.data, defs[i].name.len, defs[i].kind, hash);
        if (node == NULL) {
            ngx_shmtx_unlock(&zone->shpool->mutex);
            return NGX_ERROR;
        }

        ngx_rbtree_insert(&sh->rbtree, &node->node);
    }

    ngx_shmtx_unlock(&zone->shpool->mutex);

    return NGX_OK;
}

static void
ngx_http_wasm_metrics_rbtree_insert_value(ngx_rbtree_node_t *temp,
                                          ngx_rbtree_node_t *node,
                                          ngx_rbtree_node_t *sentinel) {
    ngx_http_wasm_metric_node_t *n, *t;
    ngx_int_t rc;

    for (;;) {
        if (node->key < temp->key) {
            rc = -1;
        } else if (node->key > temp->key) {
            rc = 1;
        } else {
            n = (ngx_http_wasm_metric_node_t *)node;
            t = (ngx_http_wasm_metric_node_t *)temp;
            rc = ngx_memn2cmp(n->data, t->data, n->name_len, t->name_len);
        }

        ngx_rbtree_node_t **p = (rc < 0) ? &temp->left : &temp->right;

        if (*p == sentinel) {
            *p = node;
            node->parent = temp;
            node->left = sentinel;
            node->right = sentinel;
            ngx_rbt_red(node);
            return;
        }

        temp = *p;
    }
}

static ngx_http_wasm_metric_node_t *
ngx_http_wasm_metrics_lookup(ngx_http_wasm_metrics_zone_t *zone,
                             const u_char *name,
                             size_t name_len,
                             uint32_t hash) {
    ngx_http_wasm_metrics_ctx_t *sh;
    ngx_http_wasm_metric_node_t *entry;
    ngx_int_t rc;
    ngx_rbtree_node_t *node, *sentinel;

    sh = zone->sh;
    if (sh == NULL) {
        return NULL;
    }

    node = sh->rbtree.root;
    sentinel = sh->rbtree.sentinel;

    while (node != sentinel) {
        if (hash < node->key) {
            node = node->left;
            continue;
        }

        if (hash > node->key) {
            node = node->right;
            continue;
        }

        entry = (ngx_http_wasm_metric_node_t *)node;
        rc = ngx_memn2cmp(
            (u_char *)name, entry->data, name_len, entry->name_len);
        if (rc == 0) {
            return entry;
        }

        node = (rc < 0) ? node->left : node->right;
    }

    return NULL;
}

static ngx_http_wasm_metric_node_t *
ngx_http_wasm_metrics_alloc_node(ngx_http_wasm_metrics_zone_t *zone,
                                 const u_char *name,
                                 size_t name_len,
                                 ngx_uint_t kind,
                                 uint32_t hash) {
    ngx_http_wasm_metric_node_t *entry;
    size_t size;

    size = offsetof(ngx_http_wasm_metric_node_t, data) + name_len;
    entry = ngx_slab_alloc_locked(zone->shpool, size);
    if (entry == NULL) {
        return NULL;
    }

    entry->node.key = hash;
    entry->name_len = name_len;
    entry->kind = kind;
    entry->value = 0;
    ngx_memcpy(entry->data, name, name_len);

    return entry;
}

ngx_int_t ngx_http_wasm_metrics_counter_inc(ngx_http_wasm_metrics_zone_t *zone,
                                            const u_char *name,
                                            size_t name_len,
                                            ngx_int_t delta) {
    ngx_http_wasm_metric_node_t *entry;
    int64_t next;
    uint32_t hash;

    if (zone == NULL || zone->shpool == NULL || zone->sh == NULL ||
        name == NULL || name_len == 0 ||
        name_len > NGX_HTTP_WASM_METRIC_MAX_NAME_LEN || delta < 0) {
        return NGX_HTTP_WASM_ERROR;
    }

    hash = ngx_crc32_short((u_char *)name, name_len);

    ngx_shmtx_lock(&zone->shpool->mutex);
    entry = ngx_http_wasm_metrics_lookup(zone, name, name_len, hash);
    if (entry == NULL || entry->kind != NGX_HTTP_WASM_METRIC_KIND_COUNTER) {
        ngx_shmtx_unlock(&zone->shpool->mutex);
        return NGX_HTTP_WASM_ERROR;
    }

    if (entry->value > LLONG_MAX - delta) {
        entry->value = LLONG_MAX;
    } else {
        next = entry->value + delta;
        entry->value = next;
    }

    ngx_shmtx_unlock(&zone->shpool->mutex);

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_metrics_gauge_set(ngx_http_wasm_metrics_zone_t *zone,
                                          const u_char *name,
                                          size_t name_len,
                                          ngx_int_t value) {
    ngx_http_wasm_metric_node_t *entry;
    uint32_t hash;

    if (zone == NULL || zone->shpool == NULL || zone->sh == NULL ||
        name == NULL || name_len == 0 ||
        name_len > NGX_HTTP_WASM_METRIC_MAX_NAME_LEN) {
        return NGX_HTTP_WASM_ERROR;
    }

    hash = ngx_crc32_short((u_char *)name, name_len);

    ngx_shmtx_lock(&zone->shpool->mutex);
    entry = ngx_http_wasm_metrics_lookup(zone, name, name_len, hash);
    if (entry == NULL || entry->kind != NGX_HTTP_WASM_METRIC_KIND_GAUGE) {
        ngx_shmtx_unlock(&zone->shpool->mutex);
        return NGX_HTTP_WASM_ERROR;
    }

    entry->value = value;
    ngx_shmtx_unlock(&zone->shpool->mutex);

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_metrics_gauge_add(ngx_http_wasm_metrics_zone_t *zone,
                                          const u_char *name,
                                          size_t name_len,
                                          ngx_int_t delta) {
    ngx_http_wasm_metric_node_t *entry;
    uint32_t hash;

    if (zone == NULL || zone->shpool == NULL || zone->sh == NULL ||
        name == NULL || name_len == 0 ||
        name_len > NGX_HTTP_WASM_METRIC_MAX_NAME_LEN) {
        return NGX_HTTP_WASM_ERROR;
    }

    hash = ngx_crc32_short((u_char *)name, name_len);

    ngx_shmtx_lock(&zone->shpool->mutex);
    entry = ngx_http_wasm_metrics_lookup(zone, name, name_len, hash);
    if (entry == NULL || entry->kind != NGX_HTTP_WASM_METRIC_KIND_GAUGE) {
        ngx_shmtx_unlock(&zone->shpool->mutex);
        return NGX_HTTP_WASM_ERROR;
    }

    entry->value += delta;
    ngx_shmtx_unlock(&zone->shpool->mutex);

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_metrics_get(ngx_http_wasm_metrics_zone_t *zone,
                                    const u_char *name,
                                    size_t name_len,
                                    ngx_uint_t *kind,
                                    int64_t *value) {
    ngx_http_wasm_metric_node_t *entry;
    uint32_t hash;

    if (zone == NULL || zone->shpool == NULL || zone->sh == NULL ||
        name == NULL || kind == NULL || value == NULL || name_len == 0 ||
        name_len > NGX_HTTP_WASM_METRIC_MAX_NAME_LEN) {
        return NGX_HTTP_WASM_ERROR;
    }

    hash = ngx_crc32_short((u_char *)name, name_len);

    ngx_shmtx_lock(&zone->shpool->mutex);
    entry = ngx_http_wasm_metrics_lookup(zone, name, name_len, hash);
    if (entry == NULL) {
        ngx_shmtx_unlock(&zone->shpool->mutex);
        return NGX_HTTP_WASM_NOT_FOUND;
    }

    *kind = entry->kind;
    *value = entry->value;
    ngx_shmtx_unlock(&zone->shpool->mutex);

    return NGX_HTTP_WASM_OK;
}
