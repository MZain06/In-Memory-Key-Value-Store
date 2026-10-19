#include "kv.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define KV_DEFAULT_CAPACITY 8
#define KV_DEFAULT_MAX_LOAD 0.75

typedef struct kv_entry {
    struct kv_entry *next;
    uint64_t hash;
    char *key;
    size_t key_len;
    void *value;
    size_t value_len;
} kv_entry;

struct kv_store {
    kv_entry **buckets;
    size_t capacity;
    size_t size;
    double max_load;
    size_t resizes;
};

static uint64_t fnv1a(const char *key, size_t len) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < len; i++) {
        hash ^= (unsigned char)key[i];
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

static size_t bucket_index(const kv_store *store, uint64_t hash) {
    return (size_t)(hash & (store->capacity - 1));
}

static kv_entry **find_link(const kv_store *store, const char *key, size_t key_len, uint64_t hash) {
    kv_entry **link = &store->buckets[bucket_index(store, hash)];
    while (*link != NULL) {
        const kv_entry *entry = *link;
        if (entry->hash == hash && entry->key_len == key_len &&
            memcmp(entry->key, key, key_len) == 0) {
            break;
        }
        link = &(*link)->next;
    }
    return link;
}

static void *copy_value(const void *value, size_t value_len) {
    void *copy = malloc(value_len > 0 ? value_len : 1);
    if (copy != NULL && value_len > 0) {
        memcpy(copy, value, value_len);
    }
    return copy;
}

static void free_entry(kv_entry *entry) {
    free(entry->key);
    free(entry->value);
    free(entry);
}

static size_t round_up_capacity(size_t requested) {
    size_t capacity = KV_DEFAULT_CAPACITY;
    while (capacity < requested) {
        if (capacity > SIZE_MAX / 2) {
            return 0;
        }
        capacity *= 2;
    }
    return capacity;
}

kv_store *kv_create(const kv_options *opts) {
    size_t requested = opts != NULL ? opts->initial_capacity : 0;
    double max_load = opts != NULL ? opts->max_load_factor : 0.0;
    if (max_load == 0.0) {
        max_load = KV_DEFAULT_MAX_LOAD;
    } else if (!(max_load > 0.0)) {
        return NULL;
    }

    size_t capacity = round_up_capacity(requested);
    if (capacity == 0) {
        return NULL;
    }

    kv_store *store = malloc(sizeof *store);
    if (store == NULL) {
        return NULL;
    }
    store->buckets = calloc(capacity, sizeof *store->buckets);
    if (store->buckets == NULL) {
        free(store);
        return NULL;
    }
    store->capacity = capacity;
    store->size = 0;
    store->max_load = max_load;
    store->resizes = 0;
    return store;
}

void kv_destroy(kv_store *store) {
    if (store == NULL) {
        return;
    }
    for (size_t i = 0; i < store->capacity; i++) {
        kv_entry *entry = store->buckets[i];
        while (entry != NULL) {
            kv_entry *next = entry->next;
            free_entry(entry);
            entry = next;
        }
    }
    free(store->buckets);
    free(store);
}

kv_status kv_put(kv_store *store, const char *key, const void *value, size_t value_len) {
    if (store == NULL || key == NULL || (value == NULL && value_len > 0)) {
        return KV_INVALID_ARG;
    }
    size_t key_len = strlen(key);
    uint64_t hash = fnv1a(key, key_len);
    kv_entry **link = find_link(store, key, key_len, hash);

    void *value_copy = copy_value(value, value_len);
    if (value_copy == NULL) {
        return KV_NO_MEMORY;
    }

    if (*link != NULL) {
        free((*link)->value);
        (*link)->value = value_copy;
        (*link)->value_len = value_len;
        return KV_OK;
    }

    kv_entry *entry = malloc(sizeof *entry);
    char *key_copy = malloc(key_len + 1);
    if (entry == NULL || key_copy == NULL) {
        free(entry);
        free(key_copy);
        free(value_copy);
        return KV_NO_MEMORY;
    }
    memcpy(key_copy, key, key_len + 1);

    size_t index = bucket_index(store, hash);
    entry->next = store->buckets[index];
    entry->hash = hash;
    entry->key = key_copy;
    entry->key_len = key_len;
    entry->value = value_copy;
    entry->value_len = value_len;
    store->buckets[index] = entry;
    store->size++;
    return KV_OK;
}

kv_status kv_get(const kv_store *store, const char *key, const void **value, size_t *value_len) {
    if (store == NULL || key == NULL) {
        return KV_INVALID_ARG;
    }
    size_t key_len = strlen(key);
    const kv_entry *entry = *find_link(store, key, key_len, fnv1a(key, key_len));
    if (value != NULL) {
        *value = entry != NULL ? entry->value : NULL;
    }
    if (value_len != NULL) {
        *value_len = entry != NULL ? entry->value_len : 0;
    }
    return entry != NULL ? KV_OK : KV_NOT_FOUND;
}

kv_status kv_delete(kv_store *store, const char *key) {
    if (store == NULL || key == NULL) {
        return KV_INVALID_ARG;
    }
    size_t key_len = strlen(key);
    kv_entry **link = find_link(store, key, key_len, fnv1a(key, key_len));
    kv_entry *entry = *link;
    if (entry == NULL) {
        return KV_NOT_FOUND;
    }
    *link = entry->next;
    free_entry(entry);
    store->size--;
    return KV_OK;
}

size_t kv_size(const kv_store *store) {
    return store != NULL ? store->size : 0;
}

void kv_get_stats(const kv_store *store, kv_stats *out) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (store == NULL) {
        return;
    }
    out->size = store->size;
    out->capacity = store->capacity;
    out->resizes = store->resizes;
    for (size_t i = 0; i < store->capacity; i++) {
        size_t length = 0;
        for (const kv_entry *entry = store->buckets[i]; entry != NULL; entry = entry->next) {
            length++;
        }
        if (length > out->longest_chain) {
            out->longest_chain = length;
        }
    }
}
