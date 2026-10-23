#include "kv.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    kv_store *store = kv_create(NULL);
    if (store == NULL) {
        return 1;
    }

    const char *keys[] = {"language", "structure", "hash"};
    const char *values[] = {"C11", "hash table", "FNV-1a"};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        if (kv_put(store, keys[i], values[i], strlen(values[i])) != KV_OK) {
            kv_destroy(store);
            return 1;
        }
    }

    const void *value = NULL;
    size_t len = 0;
    if (kv_get(store, "structure", &value, &len) == KV_OK) {
        printf("structure = %.*s\n", (int)len, (const char *)value);
    }

    kv_delete(store, "hash");
    printf("hash present: %s\n", kv_get(store, "hash", NULL, NULL) == KV_OK ? "yes" : "no");
    printf("keys stored: %zu\n", kv_size(store));

    kv_destroy(store);
    return 0;
}
