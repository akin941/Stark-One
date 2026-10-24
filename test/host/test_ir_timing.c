/*
 * test_ir_timing.c — duration matching and building (TASKS.md STARK-0301):
 * tolerance boundaries on every duration class, the absolute floor, limits.
 */
#include "ir_codec.h"
#include "ir_timing.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

/* nominal +/- (tol - 1) % matches, +/- (tol + 1) % does not. */
static void assert_boundaries(uint32_t nominal)
{
    TEST_ASSERT_TRUE(ir_match(nominal, nominal, IR_TOL_PCT));
    TEST_ASSERT_TRUE(ir_match(nominal * 124 / 100, nominal, IR_TOL_PCT));
    TEST_ASSERT_TRUE(ir_match(nominal * 76 / 100 + 1, nominal, IR_TOL_PCT));
    TEST_ASSERT_FALSE(ir_match(nominal * 126 / 100 + 1, nominal, IR_TOL_PCT));
    TEST_ASSERT_FALSE(ir_match(nominal * 74 / 100, nominal, IR_TOL_PCT));
}

static void test_boundaries_on_every_class(void)
{
    static const uint32_t classes[] = {444,  560,  600,  889,  1200, 1690,
                                       2250, 2400, 2666, 4500, 9000};
    for (size_t i = 0; i < sizeof classes / sizeof classes[0]; i++) {
        assert_boundaries(classes[i]);
    }
}

static void test_exact_edges(void)
{
    /* 560 +/- 140 */
    TEST_ASSERT_TRUE(ir_match(420, 560, 25));
    TEST_ASSERT_TRUE(ir_match(700, 560, 25));
    TEST_ASSERT_FALSE(ir_match(419, 560, 25));
    TEST_ASSERT_FALSE(ir_match(701, 560, 25));
}

static void test_floor_for_short_durations(void)
{
    /* 25 % of 100 us is 25 us; the 60 us floor applies */
    TEST_ASSERT_TRUE(ir_match(160, 100, 25));
    TEST_ASSERT_TRUE(ir_match(40, 100, 25));
    TEST_ASSERT_FALSE(ir_match(161, 100, 25));
    TEST_ASSERT_TRUE(ir_match(0, 50, 25)); /* the lower bound clamps at 0 */
}

static void test_push(void)
{
    ir_timings_t t = {0};
    TEST_ASSERT_TRUE(ir_push(&t, 1));
    TEST_ASSERT_TRUE(ir_push(&t, UINT16_MAX));
    TEST_ASSERT_FALSE(ir_push(&t, 0));
    TEST_ASSERT_FALSE(ir_push(&t, UINT16_MAX + 1u));
    TEST_ASSERT_EQUAL_UINT16(2, t.n);
    t.n = IR_MAX_DURATIONS;
    TEST_ASSERT_FALSE(ir_push(&t, 5));
    TEST_ASSERT_EQUAL_UINT16(IR_MAX_DURATIONS, t.n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_boundaries_on_every_class);
    RUN_TEST(test_exact_edges);
    RUN_TEST(test_floor_for_short_durations);
    RUN_TEST(test_push);
    return UNITY_END();
}
