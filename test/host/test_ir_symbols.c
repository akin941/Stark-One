/*
 * test_ir_symbols.c — RMT symbol words <-> timings and the capture ring
 * (TASKS.md STARK-0303/0304): every vector's nominal encoding survives
 * timings -> symbols -> timings, levels are agnostic (an inverted receiver
 * decodes the same), long durations split and merge, limits hold.
 */
#include <string.h>
#include "ir_capture.h"
#include "ir_codec.h"
#include "ir_nec_vectors.h"
#include "ir_rc5_vectors.h"
#include "ir_rc6_vectors.h"
#include "ir_sirc20_vectors.h"
#include "ir_symbols.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static uint32_t sym(uint32_t l0, uint32_t d0, uint32_t l1, uint32_t d1)
{
    return d0 | (l0 << 15) | (d1 << 16) | (l1 << 31);
}

static void identity(const ir_vector_t *vs, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        ir_timings_t t = {.n = vs[i].n};
        memcpy(t.us, vs[i].timings, vs[i].n * sizeof vs[i].timings[0]);
        uint32_t words[IR_MAX_DURATIONS];
        size_t n = 0;
        TEST_ASSERT_TRUE(ir_timings_to_symbols(&t, words, IR_MAX_DURATIONS, &n));
        ir_timings_t back;
        TEST_ASSERT_TRUE(ir_symbols_to_timings(words, n, &back));
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(t.n, back.n, vs[i].label);
        TEST_ASSERT_EQUAL_UINT16_ARRAY(t.us, back.us, t.n);
    }
}

static void test_identity_for_every_vector(void)
{
    identity(k_nec_vectors, sizeof k_nec_vectors / sizeof k_nec_vectors[0]);
    identity(k_rc5_vectors, sizeof k_rc5_vectors / sizeof k_rc5_vectors[0]);
    identity(k_rc6_vectors, sizeof k_rc6_vectors / sizeof k_rc6_vectors[0]);
    identity(k_sirc20_vectors, sizeof k_sirc20_vectors / sizeof k_sirc20_vectors[0]);
}

static void test_levels_are_agnostic_and_runs_merge(void)
{
    /* an active-low receiver: the mark is level 0 */
    uint32_t inv[] = {sym(0, 9000, 1, 4500), sym(0, 560, 1, 560), sym(0, 560, 1, 0)};
    ir_timings_t t;
    TEST_ASSERT_TRUE(ir_symbols_to_timings(inv, 3, &t));
    TEST_ASSERT_EQUAL_UINT16(5, t.n);
    TEST_ASSERT_EQUAL_UINT16(9000, t.us[0]);
    TEST_ASSERT_EQUAL_UINT16(560, t.us[4]);
    /* same-level neighbours merge, across words */
    uint32_t split[] = {sym(1, 300, 1, 260), sym(0, 400, 0, 160), sym(1, 560, 0, 0)};
    TEST_ASSERT_TRUE(ir_symbols_to_timings(split, 3, &t));
    TEST_ASSERT_EQUAL_UINT16(3, t.n);
    TEST_ASSERT_EQUAL_UINT16(560, t.us[0]);
    TEST_ASSERT_EQUAL_UINT16(560, t.us[1]);
    /* the first half zero: nothing */
    uint32_t empty[] = {sym(1, 0, 0, 0)};
    TEST_ASSERT_TRUE(ir_symbols_to_timings(empty, 1, &t));
    TEST_ASSERT_EQUAL_UINT16(0, t.n);
    TEST_ASSERT_TRUE(ir_symbols_to_timings(empty, 0, &t));
    /* one level only */
    uint32_t one[] = {sym(1, 500, 1, 500)};
    TEST_ASSERT_TRUE(ir_symbols_to_timings(one, 1, &t));
    TEST_ASSERT_EQUAL_UINT16(1, t.n);
    TEST_ASSERT_EQUAL_UINT16(1000, t.us[0]);
}

static void test_long_durations_split_and_merge(void)
{
    ir_timings_t t = {.n = 3, .us = {65535, 40000, 560}};
    uint32_t words[8];
    size_t n = 0;
    TEST_ASSERT_TRUE(ir_timings_to_symbols(&t, words, 8, &n));
    ir_timings_t back;
    TEST_ASSERT_TRUE(ir_symbols_to_timings(words, n, &back));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(t.us, back.us, 3);
    TEST_ASSERT_FALSE(ir_timings_to_symbols(&t, words, 2, &n)); /* too small */
    ir_timings_t big = {.n = 1, .us = {65535}};
    TEST_ASSERT_FALSE(ir_timings_to_symbols(&big, words, 0, &n)); /* no room while splitting */
    /* a merged run above 65535 us does not fit */
    uint32_t huge[] = {sym(1, 32767, 1, 32767), sym(1, 10, 0, 5)};
    TEST_ASSERT_FALSE(ir_symbols_to_timings(huge, 2, &back));
    uint32_t huge_last[] = {sym(1, 32767, 1, 32767), sym(1, 10, 1, 0)};
    TEST_ASSERT_FALSE(ir_symbols_to_timings(huge_last, 2, &back));
}

static void test_too_many_durations(void)
{
    static uint32_t words[IR_MAX_DURATIONS];
    for (size_t i = 0; i < IR_MAX_DURATIONS; i++) {
        words[i] = sym(1, 500, 0, 500); /* 2 durations each: 512 in all */
    }
    ir_timings_t t;
    TEST_ASSERT_FALSE(ir_symbols_to_timings(words, IR_MAX_DURATIONS, &t));
    TEST_ASSERT_TRUE(ir_symbols_to_timings(words, IR_MAX_DURATIONS / 2, &t)); /* exactly 256 */
    TEST_ASSERT_EQUAL_UINT16(IR_MAX_DURATIONS, t.n);
    words[IR_MAX_DURATIONS / 2] = sym(1, 500, 1, 0); /* one more mark */
    TEST_ASSERT_FALSE(ir_symbols_to_timings(words, IR_MAX_DURATIONS / 2 + 1, &t));
}

static void test_capture_ring(void)
{
    ir_capture_t r;
    ir_capture_init(&r);
    ir_signal_t s = {0}, out;
    TEST_ASSERT_FALSE(ir_capture_take(&r, &out));
    for (uint16_t i = 1; i <= IR_CAPTURE_SLOTS + 2; i++) {
        s.frame.command = i;
        ir_capture_push(&r, &s);
    }
    TEST_ASSERT_EQUAL_UINT32(2, r.dropped); /* the two oldest went */
    for (uint16_t i = 3; i <= IR_CAPTURE_SLOTS + 2; i++) {
        TEST_ASSERT_TRUE(ir_capture_take(&r, &out));
        TEST_ASSERT_EQUAL_UINT16(i, out.frame.command);
    }
    TEST_ASSERT_FALSE(ir_capture_take(&r, &out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_identity_for_every_vector);
    RUN_TEST(test_levels_are_agnostic_and_runs_merge);
    RUN_TEST(test_long_durations_split_and_merge);
    RUN_TEST(test_too_many_durations);
    RUN_TEST(test_capture_ring);
    return UNITY_END();
}
