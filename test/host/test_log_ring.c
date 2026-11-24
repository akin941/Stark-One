/*
 * test_log_ring.c — the capture ring (TASKS.md STARK-0206): whole-line
 * semantics, wrap, oversize text, drop counting, takes into small buffers.
 */
#include <stdio.h>
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

static size_t tail(size_t max)
{
    memset(out, 0, sizeof out);
    return log_ring_tail(&r, out, max);
}

static void test_tail_copies_the_newest_whole_lines_and_consumes_nothing(void)
{
    log_ring_put(&r, "aa\nbb\ncc\n", 9);
    TEST_ASSERT_EQUAL_size_t(9, tail(sizeof out));
    TEST_ASSERT_EQUAL_STRING("aa\nbb\ncc\n", out);
    TEST_ASSERT_EQUAL_size_t(6, tail(7)); /* the newest two lines fit in 7: "aa\n" is left out */
    TEST_ASSERT_EQUAL_STRING("bb\ncc\n", out);
    TEST_ASSERT_EQUAL_size_t(3, tail(3));
    TEST_ASSERT_EQUAL_STRING("cc\n", out);
    TEST_ASSERT_EQUAL_size_t(0, tail(2)); /* a line longer than max is left out */
    TEST_ASSERT_EQUAL_size_t(0, tail(0));
    TEST_ASSERT_EQUAL_size_t(9, log_ring_used(&r)); /* nothing was consumed */
    uint32_t d = 5;
    TEST_ASSERT_EQUAL_size_t(9, take(sizeof out, &d)); /* the storage service still gets it all */
    TEST_ASSERT_EQUAL_STRING("aa\nbb\ncc\n", out);
}

static void test_tail_leaves_out_a_line_still_being_written_and_follows_the_wrap(void)
{
    log_ring_put(&r, "ab\ncd", 5);
    TEST_ASSERT_EQUAL_size_t(3, tail(sizeof out));
    TEST_ASSERT_EQUAL_STRING("ab\n", out);
    log_ring_put(&r, "\n", 1);
    TEST_ASSERT_EQUAL_size_t(6, tail(sizeof out));
    /* fill and wrap the 16-byte ring several times: the tail is always whole lines, in order */
    for (int i = 0; i < 40; i++) {
        char line[8];
        int n = snprintf(line, sizeof line, "l%02d\n", i);
        log_ring_put(&r, line, (size_t)n);
        size_t got = tail(sizeof out);
        TEST_ASSERT_TRUE(got > 0 && got <= 16);
        TEST_ASSERT_EQUAL_CHAR('\n', out[got - 1]);
        if (i >= 4) {
            TEST_ASSERT_EQUAL_CHAR('l', out[0]); /* the first two short lines have gone */
        }
        TEST_ASSERT_EQUAL_STRING(line, out + got - 4);
    }
}

static void test_tail_of_nothing_a_partial_line_or_no_ring(void)
{
    TEST_ASSERT_EQUAL_size_t(0, tail(sizeof out));
    log_ring_put(&r, "no newline", 10);
    TEST_ASSERT_EQUAL_size_t(0, tail(sizeof out));
    log_ring_t none;
    log_ring_init(&none, buf, 0);
    TEST_ASSERT_EQUAL_size_t(0, log_ring_tail(&none, out, sizeof out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_whole_lines_only);
    RUN_TEST(test_drops_whole_oldest_lines_and_wraps);
    RUN_TEST(test_take_into_small_buffer);
    RUN_TEST(test_oversize_text_and_no_newline);
    RUN_TEST(test_zero_capacity);
    RUN_TEST(test_tail_copies_the_newest_whole_lines_and_consumes_nothing);
    RUN_TEST(test_tail_leaves_out_a_line_still_being_written_and_follows_the_wrap);
    RUN_TEST(test_tail_of_nothing_a_partial_line_or_no_ring);
    return UNITY_END();
}
