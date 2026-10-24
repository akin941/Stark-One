/*
 * test_ir_tx_guard.c — the transmit gate (TASKS.md STARK-0304, ADR-0019
 * §4): every rule at its boundary, repeat periods, the rate limit.
 */
#include "ir_codec.h"
#include "ir_tx_guard.h"
#include "unity.h"

static ir_signal_t s;
static ir_tx_plan_t plan;

void setUp(void)
{
    s = (ir_signal_t){.carrier_hz = 38000, .frame = {.protocol = IR_PROTO_NEC, .command = 0x12}};
    (void)ir_encode(&s.frame, &s.timings, &s.carrier_hz);
}
void tearDown(void)
{}

static void test_nec_plan(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, ir_tx_check(&s, 0, &plan));
    TEST_ASSERT_EQUAL_UINT32(108, plan.period_ms);
    /* 9000 + 4500 + 33 marks x 560 + 16 one-spaces x 1690 + 16 zero-spaces x 560 */
    TEST_ASSERT_EQUAL_UINT32(67980, plan.signal_us);
    TEST_ASSERT_EQUAL_UINT32(68, plan.total_ms);
    TEST_ASSERT_EQUAL(STARK_OK, ir_tx_check(&s, 3, &plan));
    TEST_ASSERT_EQUAL_UINT32(3 * 108 + 68, plan.total_ms);
}

static void test_repeats_limit(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, ir_tx_check(&s, IR_TX_MAX_REPEATS, &plan));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_tx_check(&s, IR_TX_MAX_REPEATS + 1, &plan));
}

static void test_carrier_range(void)
{
    s.carrier_hz = 29999;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_tx_check(&s, 0, &plan));
    s.carrier_hz = 30000;
    TEST_ASSERT_EQUAL(STARK_OK, ir_tx_check(&s, 0, &plan));
    s.carrier_hz = 60000;
    TEST_ASSERT_EQUAL(STARK_OK, ir_tx_check(&s, 0, &plan));
    s.carrier_hz = 60001;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_tx_check(&s, 0, &plan));
}

static void test_shape_rules(void)
{
    ir_signal_t r = {.carrier_hz = 38000, .timings = {.n = 0}};
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_tx_check(&r, 0, &plan)); /* empty */
    r.timings = (ir_timings_t){.n = 2, .us = {500, 500}};
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_tx_check(&r, 0, &plan)); /* ends on a space */
    r.timings = (ir_timings_t){.n = 3, .us = {500, 49, 500}};
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_tx_check(&r, 0, &plan)); /* below 50 us */
    r.timings.us[1] = 50;
    TEST_ASSERT_EQUAL(STARK_OK, ir_tx_check(&r, 0, &plan));
    TEST_ASSERT_EQUAL_UINT32(102, plan.period_ms); /* raw: the 1.05 ms frame (2 ms) + 100 ms */
    r.timings.n = IR_MAX_DURATIONS + 1;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_tx_check(&r, 0, &plan));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_tx_check(NULL, 0, &plan));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_tx_check(&r, 0, NULL));
}

static void test_total_time_limit(void)
{
    /* 15 x 65535 + 1 x 16975 = 1 000 000 us exactly, then one more */
    ir_signal_t r = {.carrier_hz = 38000, .timings = {.n = 17}};
    for (int i = 0; i < 15; i++) {
        r.timings.us[i] = 65535;
    }
    r.timings.us[15] = 16975 - 50;
    r.timings.us[16] = 50;
    TEST_ASSERT_EQUAL(STARK_OK, ir_tx_check(&r, 0, &plan));
    TEST_ASSERT_EQUAL_UINT32(1000000, plan.signal_us);
    TEST_ASSERT_EQUAL_UINT32(1100, plan.period_ms); /* raw: the frame + 100 ms */
    r.timings.us[16] = 51;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_tx_check(&r, 0, &plan));
    /* the repeats count towards the second */
    TEST_ASSERT_EQUAL(STARK_OK, ir_tx_check(&s, 3, &plan)); /* 4 x 67.5 ms */
    s.timings.us[1] = 65535;
    s.timings.us[3] = 65535;
    s.timings.us[5] = 65535;
    s.timings.us[7] = 65535;
    TEST_ASSERT_EQUAL(STARK_OK, ir_tx_check(&s, 0, &plan));
    /* an NEC-labelled frame longer than NEC's 108 ms period goes back to back */
    TEST_ASSERT_EQUAL_UINT32((plan.signal_us + 999u) / 1000u, plan.period_ms);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_tx_check(&s, 3, &plan)); /* 4 x ~320 ms */
}

static void test_periods(void)
{
    TEST_ASSERT_EQUAL_UINT32(108, ir_tx_period_ms(IR_PROTO_NECX, 0));
    TEST_ASSERT_EQUAL_UINT32(114, ir_tx_period_ms(IR_PROTO_RC5, 0));
    TEST_ASSERT_EQUAL_UINT32(114, ir_tx_period_ms(IR_PROTO_RC6, 0));
    TEST_ASSERT_EQUAL_UINT32(45, ir_tx_period_ms(IR_PROTO_SIRC12, 0));
    TEST_ASSERT_EQUAL_UINT32(45, ir_tx_period_ms(IR_PROTO_SIRC15, 0));
    TEST_ASSERT_EQUAL_UINT32(45, ir_tx_period_ms(IR_PROTO_SIRC20, 0));
    TEST_ASSERT_EQUAL_UINT32(150, ir_tx_period_ms(IR_PROTO_UNKNOWN, 50000));
    TEST_ASSERT_EQUAL_UINT32(101, ir_tx_period_ms(IR_PROTO_COUNT, 1));
}

static void test_rate_limit(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, ir_tx_rate(false, 0, 0, 1000));
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, ir_tx_rate(true, 5000, 5999, 1000));
    TEST_ASSERT_EQUAL(STARK_OK, ir_tx_rate(true, 5000, 6000, 1000));
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, ir_tx_rate(true, UINT32_MAX - 10, 100, 1000)); /* wrap */
    TEST_ASSERT_EQUAL(STARK_OK, ir_tx_rate(true, UINT32_MAX - 10, 990, 1000));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nec_plan);
    RUN_TEST(test_repeats_limit);
    RUN_TEST(test_carrier_range);
    RUN_TEST(test_shape_rules);
    RUN_TEST(test_total_time_limit);
    RUN_TEST(test_periods);
    RUN_TEST(test_rate_limit);
    return UNITY_END();
}
