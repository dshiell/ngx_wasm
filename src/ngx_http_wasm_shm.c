#include <ngx_http_wasm_abi.h>
#include <ngx_http_wasm_module_int.h>
#include <ngx_http_wasm_shm.h>

typedef struct {
    ngx_rbtree_t rbtree;
    ngx_rbtree_node_t sentinel;
} ngx_http_wasm_shm_ctx_t;

typedef struct {
    ngx_rbtree_node_t node;
    size_t key_len;
    size_t value_len;
    u_char data[1];
} ngx_http_wasm_shm_node_t;

static ngx_int_t ngx_http_wasm_shm_init_zone(ngx_shm_zone_t *shm_zone,
                                             void *data);
static void ngx_http_wasm_shm_rbtree_insert_value(ngx_rbtree_node_t *temp,
                                                  ngx_rbtree_node_t *node,
                                                  ngx_rbtree_node_t *sentinel);
static ngx_http_wasm_shm_node_t *
ngx_http_wasm_shm_lookup(ngx_http_wasm_shm_zone_t *zone,
                         const u_char *key,
                         size_t key_len,
                         uint32_t hash);
static ngx_http_wasm_shm_node_t *
ngx_http_wasm_shm_alloc_node(ngx_http_wasm_shm_zone_t *zone,
                             const u_char *key,
                             size_t key_len,
                             const u_char *value,
                             size_t value_len,
                             uint32_t hash);

char *ngx_http_wasm_shm_add_zone(ngx_conf_t *cf,
                                 ngx_http_wasm_shm_zone_t **dst,
                                 ngx_str_t *name,
                                 size_t size) {
    ngx_http_wasm_shm_zone_t *zone_ctx;
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

    shm_zone = ngx_shared_memory_add(cf, name, size, &ngx_http_wasm_module);
    if (shm_zone == NULL) {
        return NGX_CONF_ERROR;
    }

    if (shm_zone->data != NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG,
                           cf,
                           0,
                           "\"wasm_shm_zone\" \"%V\" is duplicate",
                           name);
        return NGX_CONF_ERROR;
    }

    shm_zone->init = ngx_http_wasm_shm_init_zone;
    shm_zone->data = zone_ctx;
    zone_ctx->zone = shm_zone;
    *dst = zone_ctx;

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_wasm_shm_init_zone(ngx_shm_zone_t *shm_zone,
                                             void *data) {
    ngx_http_wasm_shm_ctx_t *sh;
    ngx_http_wasm_shm_zone_t *old_zone, *zone;
    ngx_slab_pool_t *shpool;

    zone = shm_zone->data;
    old_zone = data;
    shpool = (ngx_slab_pool_t *)shm_zone->shm.addr;

    zone->zone = shm_zone;
    zone->shpool = shpool;

    if (old_zone != NULL) {
        zone->sh = old_zone->sh;
        return NGX_OK;
    }

    if (shpool->data != NULL) {
        zone->sh = shpool->data;
        return NGX_OK;
    }

    ngx_shmtx_lock(&shpool->mutex);

    sh = ngx_slab_alloc_locked(shpool, sizeof(*sh));
    if (sh == NULL) {
        ngx_shmtx_unlock(&shpool->mutex);
        return NGX_ERROR;
    }

    ngx_rbtree_init(
        &sh->rbtree, &sh->sentinel, ngx_http_wasm_shm_rbtree_insert_value);

    shpool->data = sh;
    zone->sh = sh;

    ngx_shmtx_unlock(&shpool->mutex);

    return NGX_OK;
}

static void ngx_http_wasm_shm_rbtree_insert_value(ngx_rbtree_node_t *temp,
                                                  ngx_rbtree_node_t *node,
                                                  ngx_rbtree_node_t *sentinel) {
    ngx_http_wasm_shm_node_t *n, *t;
    ngx_int_t rc;

    for (;;) {
        if (node->key < temp->key) {
            rc = -1;
        } else if (node->key > temp->key) {
            rc = 1;
        } else {
            n = (ngx_http_wasm_shm_node_t *)node;
            t = (ngx_http_wasm_shm_node_t *)temp;

            if (n->key_len < t->key_len) {
                rc = -1;
            } else if (n->key_len > t->key_len) {
                rc = 1;
            } else {
                rc = ngx_memn2cmp(n->data, t->data, n->key_len, t->key_len);
            }
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

static ngx_http_wasm_shm_node_t *
ngx_http_wasm_shm_lookup(ngx_http_wasm_shm_zone_t *zone,
                         const u_char *key,
                         size_t key_len,
                         uint32_t hash) {
    ngx_http_wasm_shm_ctx_t *sh;
    ngx_int_t rc;
    ngx_rbtree_node_t *node, *sentinel;
    ngx_http_wasm_shm_node_t *entry;

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

        entry = (ngx_http_wasm_shm_node_t *)node;
        rc = ngx_memn2cmp((u_char *)key, entry->data, key_len, entry->key_len);

        if (rc == 0) {
            return entry;
        }

        node = (rc < 0) ? node->left : node->right;
    }

    return NULL;
}

static ngx_http_wasm_shm_node_t *
ngx_http_wasm_shm_alloc_node(ngx_http_wasm_shm_zone_t *zone,
                             const u_char *key,
                             size_t key_len,
                             const u_char *value,
                             size_t value_len,
                             uint32_t hash) {
    ngx_http_wasm_shm_node_t *entry;
    size_t size;

    size = offsetof(ngx_http_wasm_shm_node_t, data) + key_len + value_len;
    entry = ngx_slab_alloc_locked(zone->shpool, size);
    if (entry == NULL) {
        return NULL;
    }

    entry->node.key = hash;
    entry->key_len = key_len;
    entry->value_len = value_len;

    ngx_memcpy(entry->data, key, key_len);
    ngx_memcpy(entry->data + key_len, value, value_len);

    return entry;
}

ngx_int_t ngx_http_wasm_shm_get(ngx_http_wasm_shm_zone_t *zone,
                                const u_char *key,
                                size_t key_len,
                                u_char *buf,
                                size_t buf_len) {
    ngx_http_wasm_shm_node_t *entry;
    size_t copy_len;
    uint32_t hash;

    if (zone == NULL || zone->shpool == NULL || zone->sh == NULL ||
        key == NULL || key_len == 0 ||
        key_len > NGX_HTTP_WASM_SHM_MAX_KEY_LEN) {
        return NGX_HTTP_WASM_ERROR;
    }

    hash = ngx_crc32_short((u_char *)key, key_len);

    ngx_shmtx_lock(&zone->shpool->mutex);
    entry = ngx_http_wasm_shm_lookup(zone, key, key_len, hash);
    if (entry == NULL) {
        ngx_shmtx_unlock(&zone->shpool->mutex);
        return NGX_HTTP_WASM_NOT_FOUND;
    }

    copy_len = ngx_min(entry->value_len, buf_len);
    if (copy_len != 0) {
        ngx_memcpy(buf, entry->data + entry->key_len, copy_len);
    }

    ngx_shmtx_unlock(&zone->shpool->mutex);

    return (ngx_int_t)entry->value_len;
}

ngx_int_t ngx_http_wasm_shm_set(ngx_http_wasm_shm_zone_t *zone,
                                const u_char *key,
                                size_t key_len,
                                const u_char *value,
                                size_t value_len) {
    ngx_http_wasm_shm_ctx_t *sh;
    ngx_http_wasm_shm_node_t *entry, *existing;
    uint32_t hash;

    if (zone == NULL || zone->shpool == NULL || zone->sh == NULL ||
        key == NULL || value == NULL || key_len == 0 ||
        key_len > NGX_HTTP_WASM_SHM_MAX_KEY_LEN ||
        value_len > NGX_HTTP_WASM_SHM_MAX_VALUE_LEN) {
        return NGX_HTTP_WASM_ERROR;
    }

    hash = ngx_crc32_short((u_char *)key, key_len);
    sh = zone->sh;

    ngx_shmtx_lock(&zone->shpool->mutex);

    existing = ngx_http_wasm_shm_lookup(zone, key, key_len, hash);
    if (existing != NULL && existing->value_len == value_len) {
        ngx_memcpy(existing->data + existing->key_len, value, value_len);
        ngx_shmtx_unlock(&zone->shpool->mutex);
        return NGX_HTTP_WASM_OK;
    }

    entry = ngx_http_wasm_shm_alloc_node(
        zone, key, key_len, value, value_len, hash);
    if (entry == NULL) {
        ngx_shmtx_unlock(&zone->shpool->mutex);
        return NGX_HTTP_WASM_ERROR;
    }

    if (existing != NULL) {
        ngx_rbtree_delete(&sh->rbtree, &existing->node);
        ngx_slab_free_locked(zone->shpool, existing);
    }

    ngx_rbtree_insert(&sh->rbtree, &entry->node);
    ngx_shmtx_unlock(&zone->shpool->mutex);

    return NGX_HTTP_WASM_OK;
}

ngx_int_t ngx_http_wasm_shm_delete(ngx_http_wasm_shm_zone_t *zone,
                                   const u_char *key,
                                   size_t key_len) {
    ngx_http_wasm_shm_ctx_t *sh;
    ngx_http_wasm_shm_node_t *entry;
    uint32_t hash;

    if (zone == NULL || zone->shpool == NULL || zone->sh == NULL ||
        key == NULL || key_len == 0 ||
        key_len > NGX_HTTP_WASM_SHM_MAX_KEY_LEN) {
        return NGX_HTTP_WASM_ERROR;
    }

    hash = ngx_crc32_short((u_char *)key, key_len);
    sh = zone->sh;

    ngx_shmtx_lock(&zone->shpool->mutex);
    entry = ngx_http_wasm_shm_lookup(zone, key, key_len, hash);
    if (entry != NULL) {
        ngx_rbtree_delete(&sh->rbtree, &entry->node);
        ngx_slab_free_locked(zone->shpool, entry);
    }
    ngx_shmtx_unlock(&zone->shpool->mutex);

    return NGX_HTTP_WASM_OK;
}
