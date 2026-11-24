/*
 * test_lab_pwm.c — the GPIO tool's PWM and pulse rules (components/stark_lab/lab_pwm.c, TASKS.md
 * STARK-0702, ADR-0023 §4): the limits at every edge, the LEDC arithmetic swept over every
 * frequency in range, the frequency ladder, the duty step and the pulse widths. Software only: it
 * proves the arithmetic and the bounds, never a waveform (ROADMAP V1 exit 13).
 */
#include <stdio.h>
#include "lab_pwm.h"
#include "stark_lab.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

/* AC 4: the limits at every edge. */
static void test_the_limits_at_every_edge(void)
{
    TEST_ASSERT_FALSE(lab_pwm_valid(99, 50));
    TEST_ASSERT_TRUE(lab_pwm_valid(100, 50));
    TEST_ASSERT_TRUE(lab_pwm_valid(40000, 50));
    TEST_ASSERT_FALSE(lab_pwm_valid(40001, 50));
    TEST_ASSERT_TRUE(lab_pwm_valid(1000, 0));
    TEST_ASSERT_TRUE(lab_pwm_valid(1000, 100));
    TEST_ASSERT_FALSE(lab_pwm_valid(1000, 101));
    TEST_ASSERT_FALSE(lab_pwm_valid(0, 0));
    TEST_ASSERT_FALSE(lab_pwm_valid(UINT32_MAX, UINT32_MAX));
    lab_pwm_plan_t plan;
    TEST_ASSERT_FALSE(lab_pwm_plan(99, 50, &plan));
    TEST_ASSERT_FALSE(lab_pwm_plan(40001, 50, &plan));
    TEST_ASSERT_FALSE(lab_pwm_plan(1000, 101, &plan));
    TEST_ASSERT_FALSE(lab_pwm_plan(1000, 50, NULL));
    TEST_ASSERT_TRUE(lab_pwm_plan(100, 0, &plan));
    TEST_ASSERT_TRUE(lab_pwm_plan(40000, 100, &plan));
}

/* The arithmetic: every integer frequency from 100 to 40 000 Hz is inside LEDC's divider range and
 * within 0.2 %, and the values the two ends are known. */
static void test_every_frequency_is_reachable_within_the_stated_error(void)
{
    uint32_t worst_ppm = 0;
    uint32_t worst_hz = 0;
    for (uint32_t hz = STARK_LAB_PWM_HZ_MIN; hz <= STARK_LAB_PWM_HZ_MAX; hz++) {
        lab_pwm_plan_t p;
        TEST_ASSERT_TRUE(lab_pwm_plan(hz, 50, &p));
        TEST_ASSERT_TRUE(p.divider_q8 >= LAB_PWM_DIV_Q8_MIN);
        TEST_ASSERT_TRUE(p.divider_q8 <= LAB_PWM_DIV_Q8_MAX);
        TEST_ASSERT_TRUE(p.error_ppm <= LAB_PWM_ERR_PPM_MAX);
        if (p.error_ppm > worst_ppm) {
            worst_ppm = p.error_ppm;
            worst_hz = hz;
        }
    }
    printf("pwm: worst frequency error %u ppm at %u Hz\n", (unsigned)worst_ppm, (unsigned)worst_hz);
    lab_pwm_plan_t p;
    TEST_ASSERT_TRUE(lab_pwm_plan(100, 50, &p)); /* 80 MHz / (781.25 * 1024) */
    TEST_ASSERT_EQUAL_UINT32(200000u, p.divider_q8);
    TEST_ASSERT_EQUAL_UINT32(100000u, p.achieved_mhz);
    TEST_ASSERT_EQUAL_UINT32(0u, p.error_ppm);
    TEST_ASSERT_TRUE(lab_pwm_plan(40000, 50, &p)); /* 80 MHz / (1.953125 * 1024) */
    TEST_ASSERT_EQUAL_UINT32(500u, p.divider_q8);
    TEST_ASSERT_EQUAL_UINT32(40000000u, p.achieved_mhz);
    TEST_ASSERT_TRUE(lab_pwm_plan(1000, 50, &p));
    TEST_ASSERT_EQUAL_UINT32(20000u, p.divider_q8);
}

/* Every 1 % duty step is a distinct tick count, 0 is 0 and 100 is the top of the 10-bit range
 * that stark_hal_pwm_set_duty_pct() uses. */
static void test_every_duty_step_is_a_distinct_tick_count(void)
{
    uint32_t last = 0;
    for (uint32_t d = 0; d <= LAB_PWM_DUTY_MAX; d++) {
        lab_pwm_plan_t p;
        TEST_ASSERT_TRUE(lab_pwm_plan(1000, d, &p));
        if (d == 0) {
            TEST_ASSERT_EQUAL_UINT32(0u, p.duty_ticks);
        } else {
            TEST_ASSERT_TRUE(p.duty_ticks > last);
        }
        last = p.duty_ticks;
    }
    TEST_ASSERT_EQUAL_UINT32(1023u, last);
    lab_pwm_plan_t p;
    TEST_ASSERT_TRUE(lab_pwm_plan(1000, 25, &p));
    TEST_ASSERT_EQUAL_UINT32(255u, p.duty_ticks); /* 1023 * 25 / 100 */
}

/* The divider range check is real: a source clock that puts it out of range is refused. */
static void test_a_clock_that_leaves_the_divider_range_is_refused(void)
{
    lab_pwm_plan_t p;
    TEST_ASSERT_FALSE(lab_pwm_plan_clk(1000000u, 40000, 50, &p));  /* divider below 1.0 */
    TEST_ASSERT_FALSE(lab_pwm_plan_clk(4000000000u, 100, 50, &p)); /* above 10 + 8 bits */
    TEST_ASSERT_TRUE(lab_pwm_plan_clk(40000000u, 1000, 50, &p));   /* a 40 MHz source works too */
}

/* The ladder: 100, 200, ... 900, 1000, ... 9000, 10000, ... 40000, both ways, no gaps. */
static void test_the_frequency_ladder_crosses_every_decade(void)
{
    uint32_t hz = STARK_LAB_PWM_HZ_MIN;
    unsigned steps = 0;
    for (;;) {
        uint32_t next = stark_lab_pwm_step_hz(hz, +1);
        if (next == hz) {
            break;
        }
        TEST_ASSERT_TRUE(next > hz);
        hz = next;
        steps++;
    }
    TEST_ASSERT_EQUAL_UINT32(40000u, hz);
    TEST_ASSERT_EQUAL_UINT(21, steps); /* 22 rungs: 9 + 9 + 4 */
    steps = 0;
    for (;;) {
        uint32_t next = stark_lab_pwm_step_hz(hz, -1);
        if (next == hz) {
            break;
        }
        TEST_ASSERT_TRUE(next < hz);
        hz = next;
        steps++;
    }
    TEST_ASSERT_EQUAL_UINT32(100u, hz);
    TEST_ASSERT_EQUAL_UINT(21, steps);
    TEST_ASSERT_EQUAL_UINT32(1000u, stark_lab_pwm_step_hz(900, +1));
    TEST_ASSERT_EQUAL_UINT32(900u, stark_lab_pwm_step_hz(1000, -1));
    TEST_ASSERT_EQUAL_UINT32(10000u, stark_lab_pwm_step_hz(9000, +1));
    TEST_ASSERT_EQUAL_UINT32(9000u, stark_lab_pwm_step_hz(10000, -1));
    TEST_ASSERT_EQUAL_UINT32(30000u, stark_lab_pwm_step_hz(40000, -1));
}

/* Off the ladder: the next rung in the direction; outside the range: clamped in; zero: unchanged.
 */
static void test_off_the_ladder_and_outside_the_range(void)
{
    TEST_ASSERT_EQUAL_UINT32(2000u, stark_lab_pwm_step_hz(1500, +1));
    TEST_ASSERT_EQUAL_UINT32(1000u, stark_lab_pwm_step_hz(1500, -1));
    TEST_ASSERT_EQUAL_UINT32(200u, stark_lab_pwm_step_hz(150, +1));
    TEST_ASSERT_EQUAL_UINT32(100u, stark_lab_pwm_step_hz(150, -1));
    TEST_ASSERT_EQUAL_UINT32(100u, stark_lab_pwm_step_hz(50, +1)); /* below the range: into it */
    TEST_ASSERT_EQUAL_UINT32(100u, stark_lab_pwm_step_hz(0, +1));
    TEST_ASSERT_EQUAL_UINT32(50u, stark_lab_pwm_step_hz(50, -1)); /* never moves further out */
    TEST_ASSERT_EQUAL_UINT32(40000u, stark_lab_pwm_step_hz(50000, +1));
    TEST_ASSERT_EQUAL_UINT32(40000u,
                             stark_lab_pwm_step_hz(50000, -1)); /* above the range: onto its top */
    TEST_ASSERT_EQUAL_UINT32(2500u, stark_lab_pwm_step_hz(2500, 0));
    TEST_ASSERT_EQUAL_UINT32(100u, stark_lab_pwm_step_hz(100, -1));
    TEST_ASSERT_EQUAL_UINT32(40000u, stark_lab_pwm_step_hz(40000, +1));
    /* every result of a step is a valid frequency, from every start in range */
    for (uint32_t hz = STARK_LAB_PWM_HZ_MIN; hz <= STARK_LAB_PWM_HZ_MAX; hz += 7) {
        TEST_ASSERT_TRUE(lab_pwm_valid(stark_lab_pwm_step_hz(hz, +1), 0));
        TEST_ASSERT_TRUE(lab_pwm_valid(stark_lab_pwm_step_hz(hz, -1), 0));
    }
}

static void test_duty_steps_by_one_and_clamps(void)
{
    TEST_ASSERT_EQUAL_UINT32(51u, stark_lab_pwm_step_duty(50, +1));
    TEST_ASSERT_EQUAL_UINT32(49u, stark_lab_pwm_step_duty(50, -1));
    TEST_ASSERT_EQUAL_UINT32(100u, stark_lab_pwm_step_duty(100, +1));
    TEST_ASSERT_EQUAL_UINT32(0u, stark_lab_pwm_step_duty(0, -1));
    TEST_ASSERT_EQUAL_UINT32(50u, stark_lab_pwm_step_duty(50, 0));
}

static void test_the_pulse_widths(void)
{
    const uint32_t want[LAB_PULSE_WIDTHS] = {10, 100, 1000, 10000, 100000, 1000000};
    for (size_t i = 0; i < LAB_PULSE_WIDTHS; i++) {
        TEST_ASSERT_EQUAL_UINT32(want[i], lab_pulse_width(i));
        TEST_ASSERT_TRUE(lab_pulse_valid(want[i]));
    }
    TEST_ASSERT_EQUAL_UINT32(0u, lab_pulse_width(LAB_PULSE_WIDTHS));
    TEST_ASSERT_EQUAL_UINT32(STARK_LAB_PULSE_US_MIN, lab_pulse_width(0));
    TEST_ASSERT_EQUAL_UINT32(STARK_LAB_PULSE_US_MAX, lab_pulse_width(LAB_PULSE_WIDTHS - 1u));
    for (uint32_t us = 0; us <= 2000; us++) {
        TEST_ASSERT_EQUAL(us == 10 || us == 100 || us == 1000, lab_pulse_valid(us));
    }
    TEST_ASSERT_FALSE(lab_pulse_valid(1000001u));
    TEST_ASSERT_FALSE(lab_pulse_valid(UINT32_MAX));
    TEST_ASSERT_EQUAL_UINT32(100u, stark_lab_pulse_step(10, +1));
    TEST_ASSERT_EQUAL_UINT32(10u, stark_lab_pulse_step(100, -1));
    TEST_ASSERT_EQUAL_UINT32(10u, stark_lab_pulse_step(10, -1));
    TEST_ASSERT_EQUAL_UINT32(1000000u, stark_lab_pulse_step(1000000, +1));
    TEST_ASSERT_EQUAL_UINT32(100000u, stark_lab_pulse_step(1000000, -1));
    TEST_ASSERT_EQUAL_UINT32(100u, stark_lab_pulse_step(77, 0)); /* an unlisted width: 100 us */
    TEST_ASSERT_EQUAL_UINT32(1000u, stark_lab_pulse_step(77, +1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_limits_at_every_edge);
    RUN_TEST(test_every_frequency_is_reachable_within_the_stated_error);
    RUN_TEST(test_every_duty_step_is_a_distinct_tick_count);
    RUN_TEST(test_a_clock_that_leaves_the_divider_range_is_refused);
    RUN_TEST(test_the_frequency_ladder_crosses_every_decade);
    RUN_TEST(test_off_the_ladder_and_outside_the_range);
    RUN_TEST(test_duty_steps_by_one_and_clamps);
    RUN_TEST(test_the_pulse_widths);
    return UNITY_END();
}
