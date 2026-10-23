#ifndef KV_H
#define KV_H

#include <stddef.h>

typedef struct kv_store kv_store;

/* KV_NO_MEMORY always leaves the store unchanged. */
typedef enum { KV_OK = 0, KV_NOT_FOUND, KV_NO_MEMORY, KV_INVALID_ARG } kv_status;

/* Zero fields use the defaults: capacity 8 and load factor 0.75. */
/* initial_capacity is rounded up to a power of two. */
typedef struct {
    size_t initial_capacity;
    double max_load_factor;
} kv_options;

typedef struct {
    size_t size;
    size_t capacity;
    size_t longest_chain;
    size_t resizes;
} kv_stats;

/* opts may be NULL. Returns NULL if memory runs out or max_load_factor is negative or NaN. */
kv_store *kv_create(const kv_options *opts);
void kv_destroy(kv_store *store);

/* Stores copies of key and value; value may be NULL only when value_len is 0. */
kv_status kv_put(kv_store *store, const char *key, const void *value, size_t value_len);
/* *value stays valid until this key is overwritten or deleted, or the store is destroyed. */
/* value and value_len may be NULL; on KV_NOT_FOUND they are set to NULL and 0. */
kv_status kv_get(const kv_store *store, const char *key, const void **value, size_t *value_len);
kv_status kv_delete(kv_store *store, const char *key);

size_t kv_size(const kv_store *store);
void kv_get_stats(const kv_store *store, kv_stats *out);

#endif
