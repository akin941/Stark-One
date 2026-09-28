/*
 * test_diag_core.c — diagnostics arithmetic and formatting (TASKS.md
 * STARK-0109).
 */
#include <string.h>
#include "diag_core.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static void test_fps_basic_and_rounding(void)
{
    TEST_ASSERT_EQUAL_UINT32(300, diag_fps_x10(30, 0, 1000000, 0));    /* 30.0 */
    TEST_ASSERT_EQUAL_UINT32(55, diag_fps_x10(11, 0, 2000000, 0));     /* 5.5 */
    TEST_ASSERT_EQUAL_UINT32(3, diag_fps_x10(1, 0, 3000000, 0));       /* 0.33 -> 0.3 */
    TEST_ASSERT_EQUAL_UINT32(7, diag_fps_x10(2, 0, 3000000, 0));       /* 0.67 -> 0.7 */
    TEST_ASSERT_EQUAL_UINT32(0, diag_fps_x10(5, 5, 2000000, 1000000)); /* idle */
}

static void test_fps_zero_elapsed(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, diag_fps_x10(100, 0, 5000, 5000));
}

static void test_fps_counter_and_clock_wrap(void)
{
    TEST_ASSERT_EQUAL_UINT32(200, diag_fps_x10(10, UINT32_MAX - 9, 1000000, 0)); /* 20 frames */
    TEST_ASSERT_EQUAL_UINT32(100, diag_fps_x10(10, 0, 500000, UINT64_MAX - 499999));
}

static void test_fps_saturates(void)
{
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, diag_fps_x10(UINT32_MAX, 0, 1, 0));
}

static void test_format(void)
{
    stark_diag_snapshot_t s = {.heap_free = 314312,
                               .heap_min = 300000,
                               .fps_x10 = 305,
                               .ev_dropped = 0,
                               .ui = {.overruns = 2}};
    char buf[96];
    int n = diag_format(&s, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("heap=314312 min=300000 fps=30.5 drops=0 overruns=2", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);
    s.heap_free = UINT32_MAX;
    s.ev_dropped = 7;
    s.fps_x10 = 0;
    (void)diag_format(&s, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("heap=4294967295 min=300000 fps=0.0 drops=7 overruns=2", buf);
}

static void test_format_truncates_safely(void)
{
    stark_diag_snapshot_t s = {.heap_free = 314312};
    char buf[10];
    memset(buf, 'x', sizeof buf);
    int n = diag_format(&s, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("heap=3143", buf);
    TEST_ASSERT_TRUE(n > (int)sizeof buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fps_basic_and_rounding);
    RUN_TEST(test_fps_zero_elapsed);
    RUN_TEST(test_fps_counter_and_clock_wrap);
    RUN_TEST(test_fps_saturates);
    RUN_TEST(test_format);
    RUN_TEST(test_format_truncates_safely);
    return UNITY_END();
}
