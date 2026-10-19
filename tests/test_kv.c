#include "kv.h"
#include "unity.h"

void setUp(void) {}

void tearDown(void) {}

static void test_ok_status_is_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, KV_OK);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ok_status_is_zero);
    return UNITY_END();
}
