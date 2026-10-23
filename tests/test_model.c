#include "kv.h"
#include "unity.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_COUNT 1000
#define MAX_VALUE_LEN 32

typedef struct {
    bool present;
    size_t len;
    unsigned char bytes[MAX_VALUE_LEN];
} model_entry;

static uint64_t seed = 42;
static uint64_t rng_state;
static unsigned long op_count;
static char keys[KEY_COUNT][16];
static model_entry model[KEY_COUNT];
static size_t model_size;
static double max_load;
static kv_store *store;

static uint64_t next_random(void) {
    uint64_t z = (rng_state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static void fail_at(size_t k, const char *problem) {
    char message[160];
    snprintf(message, sizeof message, "%s for %s at operation %lu (seed %" PRIu64 ")", problem,
             keys[k], op_count, seed);
    TEST_FAIL_MESSAGE(message);
}

void setUp(void) {
    rng_state = seed;
    op_count = 0;
    memset(model, 0, sizeof model);
    model_size = 0;
    max_load = 0.75;
    store = kv_create(NULL);
    TEST_ASSERT_NOT_NULL(store);
}

void tearDown(void) {
    kv_destroy(store);
    store = NULL;
}

static void check_get(size_t k) {
    const model_entry *expected = &model[k];
    const void *value = "not cleared";
    size_t len = 99;
    kv_status status = kv_get(store, keys[k], &value, &len);
    if (!expected->present) {
        if (status != KV_NOT_FOUND || value != NULL || len != 0) {
            fail_at(k, "get found a key the model does not have");
        }
    } else if (status != KV_OK) {
        fail_at(k, "get lost a key");
    } else if (len != expected->len || (len > 0 && memcmp(value, expected->bytes, len) != 0)) {
        fail_at(k, "get returned the wrong value");
    }
}

static void do_put(size_t k) {
    model_entry *entry = &model[k];
    unsigned char bytes[MAX_VALUE_LEN] = {0};
    size_t len = (size_t)(next_random() % (MAX_VALUE_LEN + 1));
    for (size_t i = 0; i < len; i++) {
        bytes[i] = (unsigned char)next_random();
    }
    if (kv_put(store, keys[k], bytes, len) != KV_OK) {
        fail_at(k, "put failed");
    }
    if (!entry->present) {
        entry->present = true;
        model_size++;
    }
    entry->len = len;
    memcpy(entry->bytes, bytes, len);
}

static void do_delete(size_t k) {
    model_entry *entry = &model[k];
    kv_status expected = entry->present ? KV_OK : KV_NOT_FOUND;
    if (kv_delete(store, keys[k]) != expected) {
        fail_at(k, "delete returned the wrong status");
    }
    if (entry->present) {
        entry->present = false;
        model_size--;
    }
}

static void check_invariants(void) {
    kv_stats stats;
    kv_get_stats(store, &stats);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(model_size, stats.size, "stats disagree with the model");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, stats.capacity & (stats.capacity - 1),
                                     "capacity is not a power of two");
    TEST_ASSERT_TRUE_MESSAGE((double)stats.size <= (double)stats.capacity * max_load,
                             "load factor is above the maximum");
}

static void step(unsigned put_percent, unsigned get_percent) {
    unsigned roll = (unsigned)(next_random() % 100);
    size_t k = (size_t)(next_random() % KEY_COUNT);
    if (roll < put_percent) {
        do_put(k);
    } else if (roll < put_percent + get_percent) {
        check_get(k);
    } else {
        do_delete(k);
    }
    if (kv_size(store) != model_size) {
        fail_at(k, "size disagrees with the model");
    }
    op_count++;
    if (op_count % 1000 == 0) {
        check_invariants();
    }
}

static void check_every_key(void) {
    for (size_t k = 0; k < KEY_COUNT; k++) {
        check_get(k);
    }
    TEST_ASSERT_EQUAL_size_t(model_size, kv_size(store));
}

static void test_random_operations_match_model(void) {
    for (int i = 0; i < 1000000; i++) {
        step(45, 35);
    }
    check_every_key();
}

static void test_growing_and_shrinking_matches_model(void) {
    for (int phase = 0; phase < 100; phase++) {
        bool filling = phase % 2 == 0;
        for (int i = 0; i < 10000; i++) {
            step(filling ? 70 : 10, 20);
        }
        kv_stats stats;
        kv_get_stats(store, &stats);
        if (filling) {
            TEST_ASSERT_EQUAL_size_t_MESSAGE(2048, stats.capacity, "table did not grow");
        } else {
            TEST_ASSERT_LESS_OR_EQUAL_size_t_MESSAGE(512, stats.capacity, "table did not shrink");
        }
    }
    check_every_key();
}

static void test_long_chains_match_model(void) {
    kv_options opts = {.max_load_factor = 4.0};
    kv_destroy(store);
    store = kv_create(&opts);
    TEST_ASSERT_NOT_NULL(store);
    max_load = 4.0;
    for (int i = 0; i < 1000000; i++) {
        step(45, 35);
    }
    check_every_key();
}

int main(int argc, char **argv) {
    if (argc > 2) {
        fprintf(stderr, "usage: %s [seed]\n", argv[0]);
        return 2;
    }
    if (argc == 2) {
        char *end = NULL;
        seed = strtoull(argv[1], &end, 10);
        if (end == argv[1] || *end != '\0') {
            fprintf(stderr, "seed must be a whole number, not '%s'\n", argv[1]);
            return 2;
        }
    }
    for (size_t k = 0; k < KEY_COUNT; k++) {
        snprintf(keys[k], sizeof keys[k], "key-%zu", k);
    }
    printf("test_model: seed %" PRIu64 "\n", seed);

    UNITY_BEGIN();
    RUN_TEST(test_random_operations_match_model);
    RUN_TEST(test_growing_and_shrinking_matches_model);
    RUN_TEST(test_long_chains_match_model);
    return UNITY_END();
}
