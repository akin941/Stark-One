/*
 * test_app_fault.c — the fault generation filter (TASKS.md STARK-0108).
 */
#include "app_internal.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static void test_current_generation_is_accepted(void)
{
    TEST_ASSERT_TRUE(app_fault_accept(7, 7, true));
}

static void test_stale_generation_is_rejected(void)
{
    TEST_ASSERT_FALSE(app_fault_accept(6, 7, true)); /* raised by the previous launch */
    TEST_ASSERT_FALSE(app_fault_accept(8, 7, true));
}

static void test_no_running_app_is_rejected(void)
{
    TEST_ASSERT_FALSE(app_fault_accept(7, 7, false));
}

static void test_generation_wrap(void)
{
    TEST_ASSERT_TRUE(app_fault_accept(0, 0, true)); /* after UINT32_MAX launches */
    TEST_ASSERT_FALSE(app_fault_accept(UINT32_MAX, 0, true));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_current_generation_is_accepted);
    RUN_TEST(test_stale_generation_is_rejected);
    RUN_TEST(test_no_running_app_is_rejected);
    RUN_TEST(test_generation_wrap);
    return UNITY_END();
}
