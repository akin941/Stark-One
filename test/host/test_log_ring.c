/*
 * test_log_ring.c — the capture ring (TASKS.md STARK-0206): whole-line
 * semantics, wrap, oversize text, drop counting, takes into small buffers.
 */
#include <string.h>
#include "log_ring.h"
#include "unity.h"

static char buf[16];
static log_ring_t r;
static char out[32];

void setUp(void)
{
    log_ring_init(&r, buf, sizeof buf);
    memset(out, 0, sizeof out);
}
void tearDown(void)
{}

static size_t take(size_t max, uint32_t *dropped)
{
    memset(out, 0, sizeof out);
    return log_ring_take(&r, out, max, dropped);
}

static void test_whole_lines_only(void)
{
    log_ring_put(&r, "ab\ncd", 5); /* "cd" is still being written */
    uint32_t d = 9;
    TEST_ASSERT_EQUAL_size_t(3, take(sizeof out, &d));
    TEST_ASSERT_EQUAL_STRING("ab\n", out);
    TEST_ASSERT_EQUAL_UINT32(0, d);
    TEST_ASSERT_EQUAL_size_t(2, log_ring_used(&r));
    TEST_ASSERT_EQUAL_size_t(0, take(sizeof out, NULL)); /* partial line waits */
    log_ring_put(&r, "\n", 1);
    TEST_ASSERT_EQUAL_size_t(3, take(sizeof out, NULL));
    TEST_ASSERT_EQUAL_STRING("cd\n", out);
}

static void test_drops_whole_oldest_lines_and_wraps(void)
{
    log_ring_put(&r, "1111\n", 5);
    log_ring_put(&r, "2222\n", 5);
    log_ring_put(&r, "3333\n", 5); /* 15 of 16 */
    log_ring_put(&r, "44\n", 3);   /* drops "1111\n" */
    uint32_t d = 0;
    TEST_ASSERT_EQUAL_size_t(13, take(sizeof out, &d));
    TEST_ASSERT_EQUAL_STRING("2222\n3333\n44\n", out);
    TEST_ASSERT_EQUAL_UINT32(1, d);
    take(sizeof out, &d);
    TEST_ASSERT_EQUAL_UINT32(0, d); /* the count restarts */
}

static void test_take_into_small_buffer(void)
{
    log_ring_put(&r, "aa\nbbbb\n", 8);
    TEST_ASSERT_EQUAL_size_t(3, take(5, NULL)); /* "bbbb\n" does not fit */
    TEST_ASSERT_EQUAL_STRING("aa\n", out);
    TEST_ASSERT_EQUAL_size_t(4, take(4, NULL)); /* one long line: a piece */
    TEST_ASSERT_EQUAL_STRING("bbbb", out);
    TEST_ASSERT_EQUAL_size_t(1, take(4, NULL));
}

static void test_oversize_text_and_no_newline(void)
{
    log_ring_put(&r, "0123456789abcdefXYZ\n", 20); /* keeps the last 16 bytes */
    TEST_ASSERT_EQUAL_size_t(16, take(sizeof out, NULL));
    TEST_ASSERT_EQUAL_STRING("456789abcdefXYZ\n", out);
    log_ring_put(&r, "0123456789abcdef", 16); /* full, no newline */
    log_ring_put(&r, "x\n", 2);               /* drops all of it */
    uint32_t d = 0;
    TEST_ASSERT_EQUAL_size_t(2, take(sizeof out, &d));
    TEST_ASSERT_EQUAL_STRING("x\n", out);
    TEST_ASSERT_EQUAL_UINT32(1, d);
}

static void test_zero_capacity(void)
{
    log_ring_t z;
    log_ring_init(&z, NULL, 0);
    log_ring_put(&z, "a\n", 2);
    TEST_ASSERT_EQUAL_size_t(0, log_ring_used(&z));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_whole_lines_only);
    RUN_TEST(test_drops_whole_oldest_lines_and_wraps);
    RUN_TEST(test_take_into_small_buffer);
    RUN_TEST(test_oversize_text_and_no_newline);
    RUN_TEST(test_zero_capacity);
    return UNITY_END();
}
