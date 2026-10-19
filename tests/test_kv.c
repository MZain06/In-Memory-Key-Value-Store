#include "kv.h"
#include "unity.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static kv_store *store;

static const kv_options crowded = {.initial_capacity = 8, .max_load_factor = 1000.0};

void setUp(void) {
    store = kv_create(NULL);
    TEST_ASSERT_NOT_NULL(store);
}

void tearDown(void) {
    kv_destroy(store);
    store = NULL;
}

static void put_string(kv_store *s, const char *key, const char *value) {
    TEST_ASSERT_EQUAL_INT(KV_OK, kv_put(s, key, value, strlen(value)));
}

static void assert_value(const kv_store *s, const char *key, const void *expected, size_t len) {
    const void *value = NULL;
    size_t value_len = 0;
    TEST_ASSERT_EQUAL_INT(KV_OK, kv_get(s, key, &value, &value_len));
    TEST_ASSERT_EQUAL_size_t(len, value_len);
    if (len > 0) {
        TEST_ASSERT_EQUAL_MEMORY(expected, value, len);
    }
}

static void assert_missing(const kv_store *s, const char *key) {
    const void *value = "not cleared";
    size_t value_len = 99;
    TEST_ASSERT_EQUAL_INT(KV_NOT_FOUND, kv_get(s, key, &value, &value_len));
    TEST_ASSERT_NULL(value);
    TEST_ASSERT_EQUAL_size_t(0, value_len);
}

static void test_new_store_is_empty(void) {
    kv_stats stats;
    kv_get_stats(store, &stats);
    TEST_ASSERT_EQUAL_size_t(0, kv_size(store));
    TEST_ASSERT_EQUAL_size_t(0, stats.size);
    TEST_ASSERT_EQUAL_size_t(8, stats.capacity);
    TEST_ASSERT_EQUAL_size_t(0, stats.longest_chain);
    TEST_ASSERT_EQUAL_size_t(0, stats.resizes);
}

static void test_capacity_rounds_up_to_power_of_two(void) {
    kv_options opts = {.initial_capacity = 100};
    kv_store *s = kv_create(&opts);
    TEST_ASSERT_NOT_NULL(s);
    kv_stats stats;
    kv_get_stats(s, &stats);
    TEST_ASSERT_EQUAL_size_t(128, stats.capacity);
    kv_destroy(s);
}

static void test_invalid_load_factor_is_rejected(void) {
    kv_options negative = {.max_load_factor = -1.0};
    kv_options not_a_number = {.max_load_factor = NAN};
    TEST_ASSERT_NULL(kv_create(&negative));
    TEST_ASSERT_NULL(kv_create(&not_a_number));
}

static void test_put_then_get_returns_value(void) {
    put_string(store, "apple", "red");
    assert_value(store, "apple", "red", 3);
    TEST_ASSERT_EQUAL_size_t(1, kv_size(store));
}

static void test_overwrite_replaces_value_and_keeps_size(void) {
    put_string(store, "colour", "blue");
    put_string(store, "colour", "a much longer value than before");
    assert_value(store, "colour", "a much longer value than before", 31);
    put_string(store, "colour", "x");
    assert_value(store, "colour", "x", 1);
    TEST_ASSERT_EQUAL_size_t(1, kv_size(store));
}

static void test_get_missing_key_returns_not_found(void) {
    put_string(store, "present", "yes");
    assert_missing(store, "absent");
}

static void test_delete_removes_only_that_key(void) {
    put_string(store, "a", "1");
    put_string(store, "b", "2");
    TEST_ASSERT_EQUAL_INT(KV_OK, kv_delete(store, "a"));
    assert_missing(store, "a");
    assert_value(store, "b", "2", 1);
    TEST_ASSERT_EQUAL_size_t(1, kv_size(store));
    put_string(store, "a", "back");
    assert_value(store, "a", "back", 4);
    TEST_ASSERT_EQUAL_size_t(2, kv_size(store));
}

static void test_delete_missing_key_returns_not_found(void) {
    put_string(store, "a", "1");
    TEST_ASSERT_EQUAL_INT(KV_NOT_FOUND, kv_delete(store, "ghost"));
    TEST_ASSERT_EQUAL_size_t(1, kv_size(store));
}

static void test_empty_key_round_trips(void) {
    put_string(store, "", "empty key");
    assert_value(store, "", "empty key", 9);
}

static void test_long_keys_round_trip(void) {
    char key[1025];
    memset(key, 'k', sizeof key - 1);
    key[sizeof key - 1] = '\0';
    put_string(store, key, "long");
    key[sizeof key - 2] = 'K';
    put_string(store, key, "different last character");
    assert_value(store, key, "different last character", 24);
    key[sizeof key - 2] = 'k';
    assert_value(store, key, "long", 4);
}

static void test_value_with_zero_bytes_round_trips(void) {
    const char bytes[] = {'a', '\0', 'b', '\0'};
    TEST_ASSERT_EQUAL_INT(KV_OK, kv_put(store, "binary", bytes, sizeof bytes));
    assert_value(store, "binary", bytes, sizeof bytes);
}

static void test_empty_value_round_trips(void) {
    const void *value = NULL;
    size_t value_len = 99;
    TEST_ASSERT_EQUAL_INT(KV_OK, kv_put(store, "nothing", NULL, 0));
    TEST_ASSERT_EQUAL_INT(KV_OK, kv_get(store, "nothing", &value, &value_len));
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_EQUAL_size_t(0, value_len);
}

static void test_put_copies_key_and_value(void) {
    char key[] = "mutable";
    char value[] = "original";
    put_string(store, key, value);
    key[0] = 'X';
    value[0] = 'X';
    assert_missing(store, key);
    put_string(store, key, value);
    assert_value(store, "mutable", "original", 8);
    assert_value(store, "Xutable", "Xriginal", 8);
}

static void test_null_arguments_are_rejected(void) {
    const void *value = NULL;
    size_t value_len = 0;
    TEST_ASSERT_EQUAL_INT(KV_INVALID_ARG, kv_put(NULL, "k", "v", 1));
    TEST_ASSERT_EQUAL_INT(KV_INVALID_ARG, kv_put(store, NULL, "v", 1));
    TEST_ASSERT_EQUAL_INT(KV_INVALID_ARG, kv_put(store, "k", NULL, 1));
    TEST_ASSERT_EQUAL_INT(KV_INVALID_ARG, kv_get(NULL, "k", &value, &value_len));
    TEST_ASSERT_EQUAL_INT(KV_INVALID_ARG, kv_get(store, NULL, &value, &value_len));
    TEST_ASSERT_EQUAL_INT(KV_INVALID_ARG, kv_delete(NULL, "k"));
    TEST_ASSERT_EQUAL_INT(KV_INVALID_ARG, kv_delete(store, NULL));
    TEST_ASSERT_EQUAL_size_t(0, kv_size(NULL));
    TEST_ASSERT_EQUAL_size_t(0, kv_size(store));
    kv_destroy(NULL);
}

static void test_many_keys_share_few_buckets(void) {
    kv_store *s = kv_create(&crowded);
    TEST_ASSERT_NOT_NULL(s);
    char key[32];
    for (int i = 0; i < 1000; i++) {
        snprintf(key, sizeof key, "key-%d", i);
        TEST_ASSERT_EQUAL_INT(KV_OK, kv_put(s, key, &i, sizeof i));
    }
    TEST_ASSERT_EQUAL_size_t(1000, kv_size(s));
    for (int i = 0; i < 1000; i++) {
        snprintf(key, sizeof key, "key-%d", i);
        assert_value(s, key, &i, sizeof i);
    }
    kv_stats stats;
    kv_get_stats(s, &stats);
    TEST_ASSERT_EQUAL_size_t(8, stats.capacity);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(125, stats.longest_chain);
    kv_destroy(s);
}

static void test_deletes_anywhere_in_long_chains(void) {
    kv_store *s = kv_create(&crowded);
    TEST_ASSERT_NOT_NULL(s);
    char key[32];
    for (int i = 0; i < 1000; i++) {
        snprintf(key, sizeof key, "key-%d", i);
        TEST_ASSERT_EQUAL_INT(KV_OK, kv_put(s, key, &i, sizeof i));
    }
    for (int i = 0; i < 1000; i += 2) {
        snprintf(key, sizeof key, "key-%d", i);
        TEST_ASSERT_EQUAL_INT(KV_OK, kv_delete(s, key));
    }
    TEST_ASSERT_EQUAL_size_t(500, kv_size(s));
    for (int i = 0; i < 1000; i++) {
        snprintf(key, sizeof key, "key-%d", i);
        if (i % 2 == 0) {
            assert_missing(s, key);
        } else {
            assert_value(s, key, &i, sizeof i);
        }
    }
    kv_destroy(s);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_new_store_is_empty);
    RUN_TEST(test_capacity_rounds_up_to_power_of_two);
    RUN_TEST(test_invalid_load_factor_is_rejected);
    RUN_TEST(test_put_then_get_returns_value);
    RUN_TEST(test_overwrite_replaces_value_and_keeps_size);
    RUN_TEST(test_get_missing_key_returns_not_found);
    RUN_TEST(test_delete_removes_only_that_key);
    RUN_TEST(test_delete_missing_key_returns_not_found);
    RUN_TEST(test_empty_key_round_trips);
    RUN_TEST(test_long_keys_round_trip);
    RUN_TEST(test_value_with_zero_bytes_round_trips);
    RUN_TEST(test_empty_value_round_trips);
    RUN_TEST(test_put_copies_key_and_value);
    RUN_TEST(test_null_arguments_are_rejected);
    RUN_TEST(test_many_keys_share_few_buckets);
    RUN_TEST(test_deletes_anywhere_in_long_chains);
    return UNITY_END();
}
