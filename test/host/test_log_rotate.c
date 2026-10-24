/*
 * test_log_rotate.c — rotation plans and names (TASKS.md STARK-0206).
 */
#include "log_rotate.h"
#include "unity.h"

static log_rotate_step_t s[LOG_ROTATE_MAX_FILES];

void setUp(void)
{}
void tearDown(void)
{}

static void test_no_rotation_when_it_fits(void)
{
    TEST_ASSERT_EQUAL_size_t(0, log_rotate_plan(100, 50, 200, 4, s, 8));
    TEST_ASSERT_EQUAL_size_t(0, log_rotate_plan(150, 50, 200, 4, s, 8)); /* exactly the limit */
    TEST_ASSERT_EQUAL_size_t(0, log_rotate_plan(0, 999, 200, 4, s, 8));  /* empty file: no split */
    TEST_ASSERT_EQUAL_size_t(0, log_rotate_plan(150, 51, 200, 0, s, 8));
    TEST_ASSERT_EQUAL_size_t(0, log_rotate_plan(150, 51, 200, 4, s, 3)); /* out too small */
    TEST_ASSERT_EQUAL_size_t(0, log_rotate_plan(UINT32_MAX, 0, UINT32_MAX, 4, s, 8));
}

static void test_four_files(void)
{
    TEST_ASSERT_EQUAL_size_t(4, log_rotate_plan(150, 51, 200, 4, s, 8));
    TEST_ASSERT_EQUAL_INT(3, s[0].from);
    TEST_ASSERT_EQUAL_INT(-1, s[0].to); /* the oldest goes */
    TEST_ASSERT_EQUAL_INT(2, s[1].from);
    TEST_ASSERT_EQUAL_INT(3, s[1].to);
    TEST_ASSERT_EQUAL_INT(1, s[2].from);
    TEST_ASSERT_EQUAL_INT(2, s[2].to);
    TEST_ASSERT_EQUAL_INT(0, s[3].from);
    TEST_ASSERT_EQUAL_INT(1, s[3].to);
}

static void test_one_and_two_files(void)
{
    TEST_ASSERT_EQUAL_size_t(1, log_rotate_plan(10, 300, 200, 1, s, 8));
    TEST_ASSERT_EQUAL_INT(0, s[0].from);
    TEST_ASSERT_EQUAL_INT(-1, s[0].to); /* just delete stark.log */
    TEST_ASSERT_EQUAL_size_t(2, log_rotate_plan(10, 300, 200, 2, s, 8));
    TEST_ASSERT_EQUAL_INT(1, s[0].from);
    TEST_ASSERT_EQUAL_INT(0, s[1].from);
    TEST_ASSERT_EQUAL_INT(1, s[1].to);
}

static void test_names(void)
{
    char n[24];
    log_rotate_name(0, n, sizeof n);
    TEST_ASSERT_EQUAL_STRING("stark.log", n);
    log_rotate_name(3, n, sizeof n);
    TEST_ASSERT_EQUAL_STRING("stark.3.log", n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_rotation_when_it_fits);
    RUN_TEST(test_four_files);
    RUN_TEST(test_one_and_two_files);
    RUN_TEST(test_names);
    return UNITY_END();
}
