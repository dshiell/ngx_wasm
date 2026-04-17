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
static ngx_int_t ngx_http_wasm_shm_key_valid(ngx_http_wasm_shm_zone_t *zone,
                                             const u_char *key,
                                             size_t key_len);
static ngx_int_t ngx_http_wasm_shm_set_locked(ngx_http_wasm_shm_zone_t *zone,
                                              const u_char *key,
                                              size_t key_len,
                                              const u_char *value,
                                              size_t value_len,
                                              uint32_t hash,
                                              ngx_uint_t require_exists,
                                              ngx_uint_t require_missing);
static ngx_int_t
ngx_http_wasm_shm_parse_i32(const u_char *data, size_t len, int32_t *value);
static size_t ngx_http_wasm_shm_format_i32(int32_t value, u_char *buf);

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

static ngx_int_t ngx_http_wasm_shm_key_valid(ngx_http_wasm_shm_zone_t *zone,
                                             const u_char *key,
                                             size_t key_len) {
    if (zone == NULL || zone->shpool == NULL || zone->sh == NULL ||
        key == NULL || key_len == 0 ||
        key_len > NGX_HTTP_WASM_SHM_MAX_KEY_LEN) {
        return 0;
    }

    return 1;
}

static ngx_int_t ngx_http_wasm_shm_set_locked(ngx_http_wasm_shm_zone_t *zone,
                                              const u_char *key,
                                              size_t key_len,
                                              const u_char *value,
                                              size_t value_len,
                                              uint32_t hash,
                                              ngx_uint_t require_exists,
                                              ngx_uint_t require_missing) {
    ngx_http_wasm_shm_ctx_t *sh;
    ngx_http_wasm_shm_node_t *entry, *existing;

    sh = zone->sh;
    existing = ngx_http_wasm_shm_lookup(zone, key, key_len, hash);

    if (require_exists && existing == NULL) {
        return NGX_HTTP_WASM_NOT_FOUND;
    }

    if (require_missing && existing != NULL) {
        return NGX_HTTP_WASM_NOT_FOUND;
    }

    if (existing != NULL && existing->value_len == value_len) {
        ngx_memcpy(existing->data + existing->key_len, value, value_len);
        return NGX_HTTP_WASM_OK;
    }

    entry = ngx_http_wasm_shm_alloc_node(
        zone, key, key_len, value, value_len, hash);
    if (entry == NULL) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (existing != NULL) {
        ngx_rbtree_delete(&sh->rbtree, &existing->node);
        ngx_slab_free_locked(zone->shpool, existing);
    }

    ngx_rbtree_insert(&sh->rbtree, &entry->node);

    return NGX_HTTP_WASM_OK;
}

static ngx_int_t
ngx_http_wasm_shm_parse_i32(const u_char *data, size_t len, int32_t *value) {
    int negative;
    int32_t result;
    uint32_t limit;
    size_t i;
    u_char ch;
    uint32_t digit;

    if (data == NULL || value == NULL || len == 0) {
        return NGX_ERROR;
    }

    i = 0;
    negative = 0;

    if (data[0] == '-') {
        negative = 1;
        i = 1;
    }

    if (i == len) {
        return NGX_ERROR;
    }

    limit = negative ? 2147483648u : 2147483647u;
    result = 0;

    for (; i < len; i++) {
        ch = data[i];
        if (ch < '0' || ch > '9') {
            return NGX_ERROR;
        }

        digit = (uint32_t)(ch - '0');
        if ((uint32_t)result > limit / 10 ||
            ((uint32_t)result == limit / 10 && digit > limit % 10)) {
            return NGX_ERROR;
        }

        result = result * 10 + (int32_t)digit;
    }

    if (negative) {
        if ((uint32_t)result == 2147483648u) {
            *value = INT32_MIN;
        } else {
            *value = -result;
        }
    } else {
        *value = result;
    }

    return NGX_OK;
}

static size_t ngx_http_wasm_shm_format_i32(int32_t value, u_char *buf) {
    u_char tmp[NGX_INT_T_LEN];
    uint32_t magnitude;
    size_t len, i;

    len = 0;
    magnitude = (value < 0) ? (uint32_t)(-(value + 1)) + 1 : (uint32_t)value;

    do {
        tmp[len++] = (u_char)('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0);

    if (value < 0) {
        buf[0] = '-';
        for (i = 0; i < len; i++) {
            buf[1 + i] = tmp[len - 1 - i];
        }
        return len + 1;
    }

    for (i = 0; i < len; i++) {
        buf[i] = tmp[len - 1 - i];
    }

    return len;
}

ngx_int_t ngx_http_wasm_shm_get(ngx_http_wasm_shm_zone_t *zone,
                                const u_char *key,
                                size_t key_len,
                                u_char *buf,
                                size_t buf_len) {
    ngx_http_wasm_shm_node_t *entry;
    size_t copy_len;
    uint32_t hash;

    if (!ngx_http_wasm_shm_key_valid(zone, key, key_len)) {
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

ngx_int_t ngx_http_wasm_shm_exists(ngx_http_wasm_shm_zone_t *zone,
                                   const u_char *key,
                                   size_t key_len) {
    ngx_http_wasm_shm_node_t *entry;
    uint32_t hash;

    if (!ngx_http_wasm_shm_key_valid(zone, key, key_len)) {
        return NGX_HTTP_WASM_ERROR;
    }

    hash = ngx_crc32_short((u_char *)key, key_len);

    ngx_shmtx_lock(&zone->shpool->mutex);
    entry = ngx_http_wasm_shm_lookup(zone, key, key_len, hash);
    ngx_shmtx_unlock(&zone->shpool->mutex);

    return (entry != NULL) ? 1 : 0;
}

ngx_int_t ngx_http_wasm_shm_incr(ngx_http_wasm_shm_zone_t *zone,
                                 const u_char *key,
                                 size_t key_len,
                                 ngx_int_t delta) {
    ngx_http_wasm_shm_node_t *entry;
    uint32_t hash;
    int32_t current, delta32, next;
    int64_t sum;
    u_char value_buf[NGX_INT_T_LEN];
    size_t value_len;
    ngx_int_t rc;

    if (!ngx_http_wasm_shm_key_valid(zone, key, key_len)) {
        return NGX_HTTP_WASM_ERROR;
    }

    if (delta < INT32_MIN || delta > INT32_MAX) {
        return NGX_HTTP_WASM_ERROR;
    }

    delta32 = (int32_t)delta;
    hash = ngx_crc32_short((u_char *)key, key_len);

    ngx_shmtx_lock(&zone->shpool->mutex);

    entry = ngx_http_wasm_shm_lookup(zone, key, key_len, hash);
    if (entry == NULL) {
        ngx_shmtx_unlock(&zone->shpool->mutex);
        return NGX_HTTP_WASM_ERROR;
    }

    if (ngx_http_wasm_shm_parse_i32(entry->data + entry->key_len,
                                    entry->value_len,
                                    &current) != NGX_OK) {
        ngx_shmtx_unlock(&zone->shpool->mutex);
        return NGX_HTTP_WASM_ERROR;
    }

    sum = (int64_t)current + delta32;
    if (sum < INT32_MIN || sum > INT32_MAX) {
        ngx_shmtx_unlock(&zone->shpool->mutex);
        return NGX_HTTP_WASM_ERROR;
    }

    next = (int32_t)sum;
    value_len = ngx_http_wasm_shm_format_i32(next, value_buf);

    rc = ngx_http_wasm_shm_set_locked(
        zone, key, key_len, value_buf, value_len, hash, 1, 0);

    ngx_shmtx_unlock(&zone->shpool->mutex);

    if (rc != NGX_HTTP_WASM_OK) {
        return rc;
    }

    return next;
}

ngx_int_t ngx_http_wasm_shm_set(ngx_http_wasm_shm_zone_t *zone,
                                const u_char *key,
                                size_t key_len,
                                const u_char *value,
                                size_t value_len) {
    uint32_t hash;
    ngx_int_t rc;

    if (!ngx_http_wasm_shm_key_valid(zone, key, key_len) || value == NULL ||
        key_len > NGX_HTTP_WASM_SHM_MAX_KEY_LEN ||
        value_len > NGX_HTTP_WASM_SHM_MAX_VALUE_LEN) {
        return NGX_HTTP_WASM_ERROR;
    }

    hash = ngx_crc32_short((u_char *)key, key_len);

    ngx_shmtx_lock(&zone->shpool->mutex);
    rc = ngx_http_wasm_shm_set_locked(
        zone, key, key_len, value, value_len, hash, 0, 0);
    ngx_shmtx_unlock(&zone->shpool->mutex);

    return rc;
}

ngx_int_t ngx_http_wasm_shm_add(ngx_http_wasm_shm_zone_t *zone,
                                const u_char *key,
                                size_t key_len,
                                const u_char *value,
                                size_t value_len) {
    uint32_t hash;
    ngx_int_t rc;

    if (!ngx_http_wasm_shm_key_valid(zone, key, key_len) || value == NULL ||
        value_len > NGX_HTTP_WASM_SHM_MAX_VALUE_LEN) {
        return NGX_HTTP_WASM_ERROR;
    }

    hash = ngx_crc32_short((u_char *)key, key_len);

    ngx_shmtx_lock(&zone->shpool->mutex);
    rc = ngx_http_wasm_shm_set_locked(
        zone, key, key_len, value, value_len, hash, 0, 1);
    ngx_shmtx_unlock(&zone->shpool->mutex);

    return rc;
}

ngx_int_t ngx_http_wasm_shm_replace(ngx_http_wasm_shm_zone_t *zone,
                                    const u_char *key,
                                    size_t key_len,
                                    const u_char *value,
                                    size_t value_len) {
    uint32_t hash;
    ngx_int_t rc;

    if (!ngx_http_wasm_shm_key_valid(zone, key, key_len) || value == NULL ||
        value_len > NGX_HTTP_WASM_SHM_MAX_VALUE_LEN) {
        return NGX_HTTP_WASM_ERROR;
    }

    hash = ngx_crc32_short((u_char *)key, key_len);

    ngx_shmtx_lock(&zone->shpool->mutex);
    rc = ngx_http_wasm_shm_set_locked(
        zone, key, key_len, value, value_len, hash, 1, 0);
    ngx_shmtx_unlock(&zone->shpool->mutex);

    return rc;
}

ngx_int_t ngx_http_wasm_shm_delete(ngx_http_wasm_shm_zone_t *zone,
                                   const u_char *key,
                                   size_t key_len) {
    ngx_http_wasm_shm_ctx_t *sh;
    ngx_http_wasm_shm_node_t *entry;
    uint32_t hash;

    if (!ngx_http_wasm_shm_key_valid(zone, key, key_len)) {
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
