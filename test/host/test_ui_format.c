/*
 * test_ui_format.c — host tests for stark_ui text helpers (STARK-0020).
 */
#include <string.h>
#include "ui_format.h"
#include "unity.h"

void setUp(void)
{}

void tearDown(void)
{}

static void assert_uptime(uint64_t seconds, const char *want)
{
    char buf[32];
    TEST_ASSERT_EQUAL_size_t(strlen(want), ui_format_uptime(seconds, buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING(want, buf);
}

void test_uptime_below_a_day(void)
{
    assert_uptime(0, "00:00:00");
    assert_uptime(59, "00:00:59");
    assert_uptime(60, "00:01:00");
    assert_uptime(3599, "00:59:59");
    assert_uptime(3600, "01:00:00");
    assert_uptime(86399, "23:59:59");
}

void test_uptime_with_days(void)
{
    assert_uptime(86400, "1d 00:00:00");
    assert_uptime(90061, "1d 01:01:01");
    assert_uptime(99u * 86400u + 7384u, "99d 02:03:04");
}

void test_uptime_truncates_but_reports_full_length(void)
{
    char buf[5];
    TEST_ASSERT_EQUAL_size_t(8, ui_format_uptime(3723, buf, sizeof buf)); /* "01:02:03" */
    TEST_ASSERT_EQUAL_STRING("01:0", buf);
    TEST_ASSERT_EQUAL_size_t(8, ui_format_uptime(3723, NULL, 0)); /* just measure */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_uptime_below_a_day);
    RUN_TEST(test_uptime_with_days);
    RUN_TEST(test_uptime_truncates_but_reports_full_length);
    return UNITY_END();
}
