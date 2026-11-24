/*
 * test_fake_lab.c — the test-only pad model (TASKS.md STARK-0701, ADR-0023 §2): hi-Z, input
 * with a pull, output with a level, external sources, wires and faults. The model carries the
 * evidence of every host and emulator test that uses it, so it is tested on its own first.
 */
#include <string.h>
#include "fake_lab.h"
#include "fake_lab_spi_vectors.h"
#include "unity.h"

#define PIN_A 38
#define PIN_B 39

void setUp(void)
{
    fake_lab_reset();
}
void tearDown(void)
{}

static bool level(int pin)
{
    bool v = false;
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_read(pin, &v));
    return v;
}

static void test_power_on_pads_are_hiz_and_read_low(void)
{
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(PIN_A));
    TEST_ASSERT_FALSE(level(PIN_A)); /* floating reads low */
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_calls());
}

static void test_input_follows_its_pull(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 1));
    TEST_ASSERT_TRUE(level(PIN_A));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 2));
    TEST_ASSERT_FALSE(level(PIN_A));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 0));
    TEST_ASSERT_FALSE(level(PIN_A));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_IN, fake_lab_mode(PIN_A));
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pull(PIN_A));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 2));
    TEST_ASSERT_EQUAL_UINT8(2, fake_lab_pull(PIN_A));
}

static void test_output_reads_back_its_level_and_hiz_releases_it(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, true));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_OUT, fake_lab_mode(PIN_A));
    TEST_ASSERT_TRUE(fake_lab_out_level(PIN_A));
    TEST_ASSERT_TRUE(level(PIN_A));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, false));
    TEST_ASSERT_FALSE(level(PIN_A));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, true));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_hiz(PIN_A));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(PIN_A));
    TEST_ASSERT_FALSE(fake_lab_out_level(PIN_A));
    TEST_ASSERT_FALSE(level(PIN_A));
}

static void test_an_external_source_wins_on_an_input_and_fights_an_output(void)
{
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_HIGH, 0, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 2));
    TEST_ASSERT_TRUE(level(PIN_A)); /* against the pull-down */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, false));
    TEST_ASSERT_TRUE(level(PIN_A)); /* contention: the pad does not follow the output */
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_LOW, 0, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 1));
    TEST_ASSERT_FALSE(level(PIN_A)); /* against the pull-up */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, false));
    TEST_ASSERT_FALSE(level(PIN_A)); /* the same level: no contention */
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_NONE, 0, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, true));
    TEST_ASSERT_TRUE(level(PIN_A));
}

static void test_square_wave_follows_the_injected_clock(void)
{
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 1000, 25); /* 1 ms period, 250 us high */
    fake_lab_time_us(0);
    TEST_ASSERT_TRUE(level(PIN_A));
    fake_lab_time_us(249);
    TEST_ASSERT_TRUE(level(PIN_A));
    fake_lab_time_us(250);
    TEST_ASSERT_FALSE(level(PIN_A));
    fake_lab_time_us(999);
    TEST_ASSERT_FALSE(level(PIN_A));
    fake_lab_time_us(1000 * 7 + 100);
    TEST_ASSERT_TRUE(level(PIN_A));
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 0, 50); /* no frequency: a 1 µs period, always high */
    TEST_ASSERT_TRUE(level(PIN_A));
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 4000000, 50); /* faster than 1 MHz: the same floor */
    TEST_ASSERT_TRUE(level(PIN_A));
}

static void test_a_wire_carries_an_output_to_another_pad(void)
{
    fake_lab_wire(PIN_A, PIN_B);
    TEST_ASSERT_FALSE(level(PIN_B)); /* PIN_A is not an output yet */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, true));
    TEST_ASSERT_TRUE(level(PIN_B));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_B, 1));
    TEST_ASSERT_TRUE(level(PIN_B));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, false));
    TEST_ASSERT_FALSE(level(PIN_B)); /* it drives low against the pull-up */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_hiz(PIN_A));
    TEST_ASSERT_TRUE(level(PIN_B)); /* released: the pull again */
    fake_lab_wire(-1, -1);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, false));
    TEST_ASSERT_TRUE(level(PIN_B)); /* no wire any more */
}

static void test_wire_limits_and_precedence(void)
{
    fake_lab_wire(100, 1); /* ignored: outside the chip */
    fake_lab_wire(1, 100);
    fake_lab_wire(1, 2);
    fake_lab_wire(3, 4);
    fake_lab_wire(5, 6);
    fake_lab_wire(7, 8);
    fake_lab_wire(9, 10); /* a fifth: ignored */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(9, true));
    TEST_ASSERT_FALSE(level(10));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(7, true));
    TEST_ASSERT_TRUE(level(8));
    fake_lab_ext(8, FAKE_LAB_EXT_LOW, 0, 0); /* an explicit source beats a wire */
    TEST_ASSERT_FALSE(level(8));
}

static void test_stuck_faults_count_down_and_filter_by_pin(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, true));
    fake_lab_fault(FAKE_LAB_FAULT_PAD_STUCK_LOW, PIN_A, 2);
    TEST_ASSERT_FALSE(level(PIN_A));
    TEST_ASSERT_FALSE(level(PIN_A));
    TEST_ASSERT_TRUE(level(PIN_A)); /* the fault is spent */
    fake_lab_fault(FAKE_LAB_FAULT_PAD_STUCK_HIGH, PIN_B, -1);
    TEST_ASSERT_TRUE(level(PIN_B));
    TEST_ASSERT_TRUE(level(PIN_B));
    TEST_ASSERT_TRUE(level(PIN_A)); /* another pad: unaffected (it is high anyway) */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, false));
    TEST_ASSERT_FALSE(level(PIN_A));
    fake_lab_fault(FAKE_LAB_FAULT_PAD_STUCK_HIGH, -1, 1); /* any pad */
    TEST_ASSERT_TRUE(level(PIN_A));
    TEST_ASSERT_FALSE(level(PIN_A));
    fake_lab_fault(FAKE_LAB_FAULT_PAD_STUCK_HIGH, -1, -1);
    fake_lab_fault(FAKE_LAB_FAULT_NONE, -1, 0); /* cleared */
    TEST_ASSERT_FALSE(level(PIN_A));
}

static void test_config_error_fails_pad_in_and_out_but_never_hiz(void)
{
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, PIN_A, 2);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_lab_pad_in(PIN_A, 1));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_lab_pad_out(PIN_A, true));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(PIN_A)); /* nothing changed */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, true));
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, -1, -1);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_hiz(PIN_A));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_lab_pad_in(PIN_B, 0));
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, PIN_B, 1); /* a fault for another pad */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 0));
}

static void test_bad_arguments_touch_nothing(void)
{
    bool v;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_in(-1, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_in(49, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_in(PIN_A, 3));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_out(-1, true));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_out(49, true));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_hiz(-1));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_hiz(49));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_read(-1, &v));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_read(49, &v));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_read(PIN_A, NULL));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_calls());
    fake_lab_ext(-1, FAKE_LAB_EXT_HIGH, 0, 0); /* ignored */
    fake_lab_ext(49, FAKE_LAB_EXT_HIGH, 0, 0);
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(-1));
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pull(49));
    TEST_ASSERT_FALSE(fake_lab_out_level(-1));
}

static void test_reset_returns_to_power_on(void)
{
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_HIGH, 0, 0);
    fake_lab_wire(PIN_A, PIN_B);
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, -1, -1);
    fake_lab_time_us(55);
    fake_lab_reset();
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_calls());
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 0));
    TEST_ASSERT_FALSE(level(PIN_A));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, true));
    TEST_ASSERT_FALSE(level(PIN_B));
}

/* ---- STARK-0702: PWM and pulse ------------------------------------------------------------ */

static void test_a_pwm_pad_reads_its_waveform_and_hiz_ends_it(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pwm_start(PIN_A, 1000, 25));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_PWM, fake_lab_mode(PIN_A));
    TEST_ASSERT_EQUAL_UINT32(1000, fake_lab_pwm_hz(PIN_A));
    TEST_ASSERT_EQUAL_UINT8(25, fake_lab_pwm_duty(PIN_A));
    fake_lab_time_us(100); /* the period is 1000 us: high for the first 250 */
    TEST_ASSERT_TRUE(level(PIN_A));
    fake_lab_time_us(300);
    TEST_ASSERT_FALSE(level(PIN_A));
    fake_lab_time_us(1100);
    TEST_ASSERT_TRUE(level(PIN_A));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_hiz(PIN_A));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(PIN_A));
    TEST_ASSERT_EQUAL_UINT32(0, fake_lab_pwm_hz(PIN_A));
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pwm_duty(PIN_A));
    fake_lab_time_us(100);
    TEST_ASSERT_FALSE(level(PIN_A));
}

static void test_a_wire_carries_a_pwm_and_an_external_source_fights_it(void)
{
    fake_lab_wire(PIN_A, PIN_B);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pwm_start(PIN_A, 1000, 50));
    unsigned high = 0;
    for (uint64_t t = 0; t < 4000; t += 10) { /* four periods, 400 samples */
        fake_lab_time_us(t);
        high += level(PIN_B) ? 1u : 0u;
    }
    TEST_ASSERT_EQUAL_UINT(200, high); /* 50 % */
    fake_lab_time_us(100);
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_LOW, 0, 0); /* a short to ground against the PWM */
    TEST_ASSERT_FALSE(level(PIN_A));
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_NONE, 0, 0);
    TEST_ASSERT_TRUE(level(PIN_A));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_hiz(PIN_A)); /* the wire carries nothing now */
    TEST_ASSERT_FALSE(level(PIN_B));
}

static void test_the_square_wave_formula(void)
{
    TEST_ASSERT_TRUE(fake_lab_square(1000, 50, 0));
    TEST_ASSERT_TRUE(fake_lab_square(1000, 50, 499));
    TEST_ASSERT_FALSE(fake_lab_square(1000, 50, 500));
    TEST_ASSERT_FALSE(fake_lab_square(1000, 0, 0));
    TEST_ASSERT_TRUE(fake_lab_square(1000, 100, 999));
    TEST_ASSERT_TRUE(fake_lab_square(0, 100, 5));       /* no frequency: a 1 us period */
    TEST_ASSERT_TRUE(fake_lab_square(2000000, 100, 5)); /* a period under 1 us counts as 1 us */
}

static void test_a_pulse_rests_at_the_opposite_level_and_is_counted(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pulse(PIN_A, true, 100));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_OUT, fake_lab_mode(PIN_A));
    TEST_ASSERT_FALSE(fake_lab_out_level(PIN_A));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_pulses());
    TEST_ASSERT_EQUAL_UINT32(100, fake_lab_last_pulse_us());
    TEST_ASSERT_TRUE(fake_lab_last_pulse_level());
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pulse(PIN_A, false, 10));
    TEST_ASSERT_TRUE(fake_lab_out_level(PIN_A));
    TEST_ASSERT_EQUAL_UINT(2, fake_lab_pulses());
    TEST_ASSERT_FALSE(fake_lab_last_pulse_level());
    fake_lab_reset();
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_pulses());
    TEST_ASSERT_EQUAL_UINT32(0, fake_lab_last_pulse_us());
}

static void test_pwm_and_pulse_obey_the_config_fault_and_bad_arguments(void)
{
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, PIN_A, 2);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_lab_pwm_start(PIN_A, 1000, 50));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_lab_pulse(PIN_A, true, 100));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(PIN_A)); /* neither took effect */
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_pulses());
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pwm_start(PIN_A, 1000, 50)); /* the count ran out */
    unsigned calls = fake_lab_calls();
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pwm_start(-1, 1000, 50));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pwm_start(49, 1000, 50));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pwm_start(PIN_A, 1000, 101));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pulse(49, true, 100));
    TEST_ASSERT_EQUAL_UINT(calls, fake_lab_calls());
    TEST_ASSERT_EQUAL_UINT32(0, fake_lab_pwm_hz(-1));
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pwm_duty(49));
}

/* ---- edge capture (STARK-0703) --------------------------------------------------------------- */

static uint32_t s_ts[5000];
static size_t s_n;
static uint32_t s_seen;
static bool s_first;

static stark_err_t edges(int pin, uint32_t window_us, size_t cap)
{
    s_n = 12345;
    s_seen = 12345;
    return fake_lab_pad_edges(pin, window_us, cap > 0u ? s_ts : NULL, cap, &s_n, &s_seen, &s_first);
}

static void test_a_steady_pad_has_no_edge_and_its_level_is_the_first_level(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 16));
    TEST_ASSERT_EQUAL_UINT(0, s_n);
    TEST_ASSERT_EQUAL_UINT32(0, s_seen);
    TEST_ASSERT_FALSE(s_first); /* hi-Z, floating */
    fake_lab_pad_in(PIN_A, 1);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 16));
    TEST_ASSERT_TRUE(s_first); /* the pull-up */
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_LOW, 0, 0);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 16));
    TEST_ASSERT_FALSE(s_first);
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_HIGH, 0, 0);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 16));
    TEST_ASSERT_TRUE(s_first);
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_NONE, 0, 0);
    fake_lab_pad_out(PIN_A, true); /* an output with nothing against it is steady at its level */
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 16));
    TEST_ASSERT_TRUE(s_first);
    TEST_ASSERT_EQUAL_UINT32(0, s_seen);
}

static void test_a_square_source_gives_its_edges_at_240_mhz(void)
{
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 1000, 25);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 64));
    TEST_ASSERT_EQUAL_UINT(40, s_n); /* 20 periods, two edges each */
    TEST_ASSERT_EQUAL_UINT32(40, s_seen);
    TEST_ASSERT_FALSE(s_first); /* the first edge is a rising one, at tick 0 */
    TEST_ASSERT_EQUAL_UINT32(0, s_ts[0]);
    TEST_ASSERT_EQUAL_UINT32(60000, s_ts[1]); /* 25 % of 240000 ticks */
    TEST_ASSERT_EQUAL_UINT32(240000, s_ts[2]);
    TEST_ASSERT_EQUAL_UINT32(300000, s_ts[3]);
    /* a window that starts inside the high part: the first edge is the falling one */
    fake_lab_time_us(123);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 10000, 64));
    TEST_ASSERT_TRUE(s_first);
    TEST_ASSERT_EQUAL_UINT32(60000, s_ts[0]);
    TEST_ASSERT_EQUAL_UINT(20, s_n);
    /* and one that starts in the low part */
    fake_lab_time_us(500);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 10000, 64));
    TEST_ASSERT_FALSE(s_first);
    TEST_ASSERT_EQUAL_UINT32(240000, s_ts[0]);
    /* 50 kHz at 50 %: 1000 edges in 10 ms */
    fake_lab_time_us(0);
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 50000, 50);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 10000, 2000));
    TEST_ASSERT_EQUAL_UINT(1000, s_n);
    TEST_ASSERT_EQUAL_UINT32(2400, s_ts[1] - s_ts[0]);
}

static void test_the_ring_is_bounded_and_the_count_saturates(void)
{
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 1000, 50);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 8));
    TEST_ASSERT_EQUAL_UINT(8, s_n);
    TEST_ASSERT_EQUAL_UINT32(9, s_seen); /* cap + 1: the ring overflowed, and that is all it says */
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 40)); /* exactly full: no overflow */
    TEST_ASSERT_EQUAL_UINT(40, s_n);
    TEST_ASSERT_EQUAL_UINT32(40, s_seen);
    /* a gate (cap 0, no ring) counts and stores nothing */
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 0));
    TEST_ASSERT_EQUAL_UINT(0, s_n);
    TEST_ASSERT_EQUAL_UINT32(40, s_seen);
    /* and stops counting at its own limit however fast the source is */
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 200000, 50);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 0));
    TEST_ASSERT_EQUAL_UINT32(FAKE_LAB_GATE_LIMIT, s_seen);
    TEST_ASSERT_EQUAL_UINT32(4096, FAKE_LAB_GATE_LIMIT);
}

static void test_a_pwm_pad_and_its_wire_are_square_sources(void)
{
    fake_lab_pad_in(PIN_B, 0);
    fake_lab_wire(PIN_A, PIN_B);
    fake_lab_pwm_start(PIN_A, 2000, 50);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_B, 10000, 64)); /* the wire */
    TEST_ASSERT_EQUAL_UINT(40, s_n);
    TEST_ASSERT_EQUAL_UINT32(60000, s_ts[1] - s_ts[0]);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 10000, 64)); /* the pad itself */
    TEST_ASSERT_EQUAL_UINT(40, s_n);
    fake_lab_ext(PIN_B, FAKE_LAB_EXT_HIGH, 0, 0); /* an external source beats the wire */
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_B, 10000, 64));
    TEST_ASSERT_EQUAL_UINT(0, s_n);
    TEST_ASSERT_TRUE(s_first);
}

static void test_a_degenerate_wave_is_steady(void)
{
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 0, 50); /* no frequency */
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 8));
    TEST_ASSERT_EQUAL_UINT32(0, s_seen);
    TEST_ASSERT_FALSE(s_first);
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 1000, 0); /* never high */
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 8));
    TEST_ASSERT_EQUAL_UINT32(0, s_seen);
    TEST_ASSERT_FALSE(s_first);
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 1000, 100); /* always high */
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 8));
    TEST_ASSERT_EQUAL_UINT32(0, s_seen);
    TEST_ASSERT_TRUE(s_first);
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 200000000u, 50); /* under two ticks a period */
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 8));
    TEST_ASSERT_EQUAL_UINT32(0, s_seen);
    TEST_ASSERT_FALSE(s_first);
}

static void test_jitter_is_bounded_deterministic_and_keeps_order(void)
{
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 1000, 50);
    fake_lab_edge_noise(480, 5);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 64));
    uint32_t first_run[64];
    size_t moved = 0;
    for (size_t i = 0; i < s_n; i++) {
        first_run[i] = s_ts[i];
        int64_t nominal = (int64_t)(i / 2) * 240000 + (int64_t)(i % 2) * 120000;
        int64_t d = (int64_t)s_ts[i] - nominal;
        TEST_ASSERT_TRUE(d >= -480 && d <= 480);
        moved += d != 0 ? 1u : 0u;
        if (i > 0) {
            TEST_ASSERT_TRUE(s_ts[i] > s_ts[i - 1]);
        }
    }
    TEST_ASSERT_TRUE(moved > 20u);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 64));
    for (size_t i = 0; i < s_n; i++) {
        TEST_ASSERT_EQUAL_UINT32(first_run[i], s_ts[i]);
    }
    fake_lab_edge_noise(480, 6);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 64));
    bool differs = false;
    for (size_t i = 0; i < s_n; i++) {
        differs = differs || first_run[i] != s_ts[i];
    }
    TEST_ASSERT_TRUE(differs);
    /* jitter wider than a pulse never reorders: 50 kHz at 1 % is a 48-tick pulse */
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 50000, 1);
    fake_lab_edge_noise(400, 9);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 10000, 2000));
    for (size_t i = 1; i < s_n; i++) {
        TEST_ASSERT_TRUE(s_ts[i] > s_ts[i - 1]);
    }
    fake_lab_edge_noise(0, 0);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 10000, 2000));
    TEST_ASSERT_EQUAL_UINT32(48, s_ts[1] - s_ts[0]); /* off again */
}

static void test_glitches_are_half_microsecond_pairs_where_there_is_room(void)
{
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 1000, 50);
    fake_lab_edge_glitches(4);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 200));
    TEST_ASSERT_TRUE(s_n > 40u);
    size_t pairs = 0;
    for (size_t i = 1; i < s_n; i++) {
        TEST_ASSERT_TRUE(s_ts[i] > s_ts[i - 1]);
        pairs += (s_ts[i] - s_ts[i - 1] == 120u) ? 1u : 0u;
    }
    TEST_ASSERT_TRUE(pairs >= 8u);
    TEST_ASSERT_EQUAL_UINT(0, s_n % 2); /* a pair keeps the level: edges still alternate */
    /* a wave with no room (300-tick pulses) gets none */
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 400000, 50);
    fake_lab_edge_glitches(1);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 10000, 5000));
    for (size_t i = 1; i < s_n; i++) {
        TEST_ASSERT_EQUAL_UINT32(300, s_ts[i] - s_ts[i - 1]);
    }
    /* the limit can fall inside a pair */
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 1000, 50);
    for (size_t cap = 1; cap < 8; cap++) {
        TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, cap));
        TEST_ASSERT_EQUAL_UINT32(cap + 1u, s_seen);
        TEST_ASSERT_EQUAL_UINT(cap, s_n);
    }
    fake_lab_edge_glitches(0);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 200));
    TEST_ASSERT_EQUAL_UINT(40, s_n);
}

static void test_the_epoch_puts_a_window_across_the_counter_wrap(void)
{
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 1000, 50);
    fake_lab_edge_epoch(0xFFFFFFFFu - 1000000u);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 64));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu - 1000000u, s_ts[0]);
    TEST_ASSERT_TRUE(s_ts[s_n - 1] < s_ts[0]); /* it wrapped */
    fake_lab_edge_epoch(0);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 64));
    TEST_ASSERT_EQUAL_UINT32(0, s_ts[0]);
}

static void test_edge_faults_and_bad_arguments(void)
{
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 1000, 50);
    fake_lab_fault(FAKE_LAB_FAULT_PAD_STUCK_LOW, PIN_A, 1);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 8));
    TEST_ASSERT_EQUAL_UINT32(0, s_seen);
    TEST_ASSERT_FALSE(s_first);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 8)); /* the one use is spent */
    TEST_ASSERT_EQUAL_UINT32(9, s_seen);
    fake_lab_fault(FAKE_LAB_FAULT_PAD_STUCK_HIGH, -1, -1);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_B, 20000, 8));
    TEST_ASSERT_EQUAL_UINT32(0, s_seen);
    TEST_ASSERT_TRUE(s_first);
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, PIN_A, 1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, edges(PIN_A, 20000, 8));
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 8));

    unsigned calls = fake_lab_calls();
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, edges(-1, 20000, 8));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, edges(49, 20000, 8));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, edges(PIN_A, 0, 8));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      fake_lab_pad_edges(PIN_A, 20000, NULL, 8, &s_n, &s_seen, &s_first));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      fake_lab_pad_edges(PIN_A, 20000, s_ts, 8, NULL, &s_seen, &s_first));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      fake_lab_pad_edges(PIN_A, 20000, s_ts, 8, &s_n, NULL, &s_first));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      fake_lab_pad_edges(PIN_A, 20000, s_ts, 8, &s_n, &s_seen, NULL));
    TEST_ASSERT_EQUAL_UINT(calls, fake_lab_calls());
    /* the capture changes nothing about the pad */
    fake_lab_pad_in(PIN_A, 1);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 8));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_IN, fake_lab_mode(PIN_A));
    TEST_ASSERT_EQUAL_UINT8(1, fake_lab_pull(PIN_A));
    fake_lab_edge_noise(5, 5);
    fake_lab_edge_glitches(3);
    fake_lab_edge_epoch(7);
    fake_lab_reset(); /* back to power on, imperfections included */
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 1000, 50);
    TEST_ASSERT_EQUAL(STARK_OK, edges(PIN_A, 20000, 64));
    TEST_ASSERT_EQUAL_UINT(40, s_n);
    TEST_ASSERT_EQUAL_UINT32(0, s_ts[0]);
}

/* ---- the I2C bus (STARK-0704) ---------------------------------------------------------------- */

static unsigned s_ext_probes;
static unsigned s_ext_xfers;

static stark_err_t ext_probe(uint8_t addr)
{
    s_ext_probes++;
    return addr == 0x50 ? STARK_OK : STARK_ERR_NOT_FOUND;
}

static stark_err_t ext_xfer(uint8_t addr, const uint8_t *w, size_t wn, uint8_t *r, size_t rn)
{
    (void)w;
    (void)wn;
    s_ext_xfers++;
    if (addr != 0x50) {
        return STARK_ERR_NOT_FOUND;
    }
    for (size_t i = 0; i < rn; i++) {
        r[i] = 0x5A;
    }
    return STARK_OK;
}

static void test_the_bus_has_the_five_devices_and_nothing_else_at_power_on(void)
{
    unsigned found = 0;
    for (unsigned a = 0x08; a <= 0x77; a++) {
        stark_err_t err = fake_lab_i2c_probe((uint8_t)a);
        if (err == STARK_OK) {
            TEST_ASSERT_TRUE(a == 0x23 || a == 0x3c || a == 0x48 || a == 0x68 || a == 0x76);
            found++;
        } else {
            TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, err);
        }
    }
    TEST_ASSERT_EQUAL_UINT(5, found);
    TEST_ASSERT_EQUAL_UINT(112, fake_lab_i2c_calls());
    uint8_t b[2];
    uint8_t w = 0;
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_lab_i2c_xfer(0x10, &w, 1, b, 2)); /* no ext bound */
    fake_lab_i2c_devices(0);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_lab_i2c_probe(0x68));
    fake_lab_reset(); /* the reset puts all five back */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_probe(0x68));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_i2c_calls());
}

static void test_each_device_answers_as_documented(void)
{
    uint8_t b[4];
    uint8_t reg = 0x75;
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_xfer(0x68, &reg, 1, b, 1));
    TEST_ASSERT_EQUAL_HEX8(0x68, b[0]);
    reg = 0xd0;
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_xfer(0x76, &reg, 1, b, 1));
    TEST_ASSERT_EQUAL_HEX8(0x60, b[0]);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_xfer(0x23, NULL, 0, b, 3));
    TEST_ASSERT_EQUAL_HEX8(0x01, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0x23, b[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, b[2]);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_xfer(0x3c, NULL, 0, b, 2));
    TEST_ASSERT_EQUAL_HEX8(0x00, b[0]);
    reg = 2; /* 0x48: pointer then pairs */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_xfer(0x48, &reg, 1, b, 2));
    TEST_ASSERT_EQUAL_HEX8(0x4b, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, b[1]);
    const uint8_t set[3] = {3, 0xAB, 0xCD};
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_xfer(0x48, set, 3, NULL, 0));
    TEST_ASSERT_EQUAL_HEX16(0xABCD, fake_lab_i2c_word(3));
    TEST_ASSERT_EQUAL_HEX16(0xABCD, fake_lab_i2c_word(7)); /* the pointer is two bits */
    const uint8_t ro[3] = {0, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_xfer(0x48, ro, 3, NULL, 0));
    TEST_ASSERT_EQUAL_HEX16(0x1900, fake_lab_i2c_word(0)); /* register 0 ignores it */
}

static void test_the_register_files_store_auto_increment_and_wrap(void)
{
    const uint8_t w[4] = {0xFE, 0x11, 0x22,
                          0x33}; /* the pointer 0xFE, then three bytes: wraps to 0 */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_xfer(0x68, w, 4, NULL, 0));
    TEST_ASSERT_EQUAL_HEX8(0x11, fake_lab_i2c_reg(0x68, 0xFE));
    TEST_ASSERT_EQUAL_HEX8(0x22, fake_lab_i2c_reg(0x68, 0xFF));
    TEST_ASSERT_EQUAL_HEX8(0x33, fake_lab_i2c_reg(0x68, 0x00));
    uint8_t b[3];
    const uint8_t at = 0xFF;
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_xfer(0x68, &at, 1, b, 3)); /* the read wraps too */
    TEST_ASSERT_EQUAL_HEX8(0x22, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0x33, b[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, b[2]);
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_i2c_writes());
    /* the other file, and an address that has none */
    const uint8_t w76[2] = {0xF4, 0x27};
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_xfer(0x76, w76, 2, NULL, 0));
    TEST_ASSERT_EQUAL_HEX8(0x27, fake_lab_i2c_reg(0x76, 0xF4));
    TEST_ASSERT_EQUAL_HEX8(0x00, fake_lab_i2c_reg(0x68, 0xF4));
    TEST_ASSERT_EQUAL_HEX8(0x00, fake_lab_i2c_reg(0x50, 0x00));
    /* the id registers ignore a write and it is not counted as one */
    unsigned writes = fake_lab_i2c_writes();
    const uint8_t id[2] = {0xD0, 0x00};
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_xfer(0x76, id, 2, NULL, 0));
    TEST_ASSERT_EQUAL_HEX8(0x60, fake_lab_i2c_reg(0x76, 0xD0));
    TEST_ASSERT_EQUAL_UINT(writes, fake_lab_i2c_writes());
}

static void test_other_addresses_belong_to_the_bound_fakes(void)
{
    s_ext_probes = 0;
    s_ext_xfers = 0;
    fake_lab_i2c_ext(ext_probe, ext_xfer);
    uint8_t b[2];
    uint8_t w = 0;
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_probe(0x50));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_lab_i2c_probe(0x51));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_xfer(0x50, &w, 1, b, 2));
    TEST_ASSERT_EQUAL_HEX8(0x5A, b[1]);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_lab_i2c_xfer(0x51, &w, 1, b, 2));
    TEST_ASSERT_EQUAL_UINT(2, s_ext_probes);
    TEST_ASSERT_EQUAL_UINT(2, s_ext_xfers);
    /* the five never reach them */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_probe(0x23));
    TEST_ASSERT_EQUAL_UINT(2, s_ext_probes);
    /* a fault comes first, for the bound fakes too */
    fake_lab_i2c_fault(FAKE_LAB_I2C_NACK_ALL, -1, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_lab_i2c_probe(0x50));
    TEST_ASSERT_EQUAL_UINT(2, s_ext_probes);
}

static void test_i2c_faults(void)
{
    uint8_t b[1];
    uint8_t w = 0x75;
    fake_lab_i2c_fault(FAKE_LAB_I2C_NACK_ALL, -1, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_lab_i2c_probe(0x68));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_lab_i2c_xfer(0x68, &w, 1, b, 1));
    fake_lab_i2c_fault(FAKE_LAB_I2C_STUCK, -1, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, fake_lab_i2c_probe(0x10));
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, fake_lab_i2c_xfer(0x68, &w, 1, b, 1));
    fake_lab_i2c_fault(FAKE_LAB_I2C_FAULT_NONE, -1, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_probe(0x68));
    /* a timeout at one address, counted */
    fake_lab_i2c_fault(FAKE_LAB_I2C_TIMEOUT, 0x68, 2);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_probe(0x48)); /* another address is not hit */
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, fake_lab_i2c_probe(0x68));
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, fake_lab_i2c_xfer(0x68, &w, 1, b, 1));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_probe(0x68)); /* spent */
    /* a timeout on any address, until cleared */
    fake_lab_i2c_fault(FAKE_LAB_I2C_TIMEOUT, -1, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, fake_lab_i2c_probe(0x23));
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, fake_lab_i2c_probe(0x23));
    fake_lab_i2c_fault(FAKE_LAB_I2C_TIMEOUT, -1, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_probe(0x23));
    /* a device that answers once and vanishes; re-arming brings it back for one more */
    fake_lab_i2c_fault(FAKE_LAB_I2C_DROP_DEVICE, 0x3c, -1);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_probe(0x3c));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_lab_i2c_probe(0x3c));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_probe(0x23)); /* the others stay */
    fake_lab_i2c_fault(FAKE_LAB_I2C_DROP_DEVICE, 0x3c, -1);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_probe(0x3c));
    /* a fault kind that is not one changes nothing */
    fake_lab_i2c_fault(FAKE_LAB_I2C_FAULT_COUNT, -1, -1);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_probe(0x68));
}

static void test_a_command_write_to_the_light_sensor_counts_and_a_pointer_alone_does_not(void)
{
    const uint8_t cmd[2] = {0x10, 0x01};
    unsigned writes = fake_lab_i2c_writes();
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_xfer(0x23, cmd, 1, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(writes, fake_lab_i2c_writes());
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_i2c_xfer(0x23, cmd, 2, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(writes + 1u, fake_lab_i2c_writes());
}

/* ---- the UART peer (STARK-0705) ----------------------------------------------------------------
 */

static size_t uart_read(uint8_t *buf, size_t max, uint32_t *fe, uint32_t *pe, uint32_t *ov)
{
    return fake_lab_uart_read(buf, max, fe, pe, ov);
}

static void test_the_uart_bits_per_character(void)
{
    TEST_ASSERT_EQUAL_UINT32(10, fake_lab_uart_bits(0)); /* 8N1 */
    TEST_ASSERT_EQUAL_UINT32(11, fake_lab_uart_bits(1)); /* 8E1 */
    TEST_ASSERT_EQUAL_UINT32(11, fake_lab_uart_bits(2)); /* 8O1 */
    TEST_ASSERT_EQUAL_UINT32(11, fake_lab_uart_bits(3)); /* 8N2 */
    TEST_ASSERT_EQUAL_UINT32(10, fake_lab_uart_bits(4)); /* 7E1 */
    TEST_ASSERT_EQUAL_UINT32(0, fake_lab_uart_bits(5));
}

static void test_the_uart_opens_closes_and_refuses_bad_settings(void)
{
    uint8_t b[4] = {1, 2, 3, 4};
    TEST_ASSERT_FALSE(fake_lab_uart_is_open());
    TEST_ASSERT_FALSE(fake_lab_uart_tx());
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_uart_write(b, 4)); /* closed */
    TEST_ASSERT_EQUAL_UINT(0, uart_read(b, 4, NULL, NULL, NULL));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, fake_lab_uart_set_baud(9600));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_uart_open(0, 0, 256, false));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_uart_open(9600, 5, 256, false));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_uart_open(9600, 0, 0, false));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(9600, 0, 256, false));
    TEST_ASSERT_TRUE(fake_lab_uart_is_open());
    TEST_ASSERT_FALSE(fake_lab_uart_tx());
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, fake_lab_uart_open(9600, 0, 256, false));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_uart_write(b, 4)); /* open without TX: nothing goes out */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_uart_set_baud(0));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_set_baud(115200));
    TEST_ASSERT_EQUAL_UINT32(87, fake_lab_uart_wire_us(1)); /* 10 bits at 115 200: 86.8 us */
    fake_lab_uart_close();
    TEST_ASSERT_FALSE(fake_lab_uart_is_open());
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(9600, 0, 256, true));
    TEST_ASSERT_TRUE(fake_lab_uart_tx());
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_uart_write(NULL, 4));
    TEST_ASSERT_EQUAL_UINT(0, uart_read(NULL, 4, NULL, NULL, NULL));
    fake_lab_uart_peer(FAKE_LAB_UART_MODES, 9600, 0); /* ignored */
    fake_lab_uart_peer(FAKE_LAB_UART_LOOP, 9600, 5);  /* ignored */
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_uart_write(NULL, 0));
}

static void test_a_loopback_returns_each_character_after_it_crossed_the_wire(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_LOOP, 0, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(9600, 0, 256, true));
    fake_lab_time_us(1000);
    const uint8_t out[5] = {10, 20, 30, 40, 50};
    TEST_ASSERT_EQUAL_UINT(5, fake_lab_uart_write(out, 5));
    uint8_t in[8];
    uint32_t fe = 0;
    uint32_t pe = 0;
    uint32_t ov = 0;
    TEST_ASSERT_EQUAL_UINT(0, uart_read(in, 8, &fe, &pe, &ov)); /* nothing has crossed yet */
    TEST_ASSERT_EQUAL_UINT32(5209, fake_lab_uart_wire_us(5));   /* 5 x 1041.7 us */
    fake_lab_time_us(1000 + 2084);                              /* two characters */
    TEST_ASSERT_EQUAL_UINT(2, uart_read(in, 8, &fe, &pe, &ov));
    TEST_ASSERT_EQUAL_UINT8(10, in[0]);
    TEST_ASSERT_EQUAL_UINT8(20, in[1]);
    fake_lab_time_us(1000 + 5300);
    TEST_ASSERT_EQUAL_UINT(2, uart_read(in, 2, &fe, &pe, &ov)); /* the caller's room */
    TEST_ASSERT_EQUAL_UINT(1, uart_read(in, 8, &fe, &pe, &ov));
    TEST_ASSERT_EQUAL_UINT8(50, in[0]);
    TEST_ASSERT_EQUAL_UINT32(0, fe + pe + ov);
    /* a second write while the line is busy queues behind the first */
    TEST_ASSERT_EQUAL_UINT(2, fake_lab_uart_write(out, 2));
    TEST_ASSERT_EQUAL_UINT(2, fake_lab_uart_write(out, 2));
    fake_lab_time_us(1000 + 5300 + 2100);
    TEST_ASSERT_EQUAL_UINT(2, uart_read(in, 8, &fe, &pe, &ov));
    fake_lab_time_us(1000 + 5300 + 4300);
    TEST_ASSERT_EQUAL_UINT(2, uart_read(in, 8, &fe, &pe, &ov));
}

static void test_the_loopback_queue_is_bounded_and_counts_what_it_drops(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_LOOP, 0, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(115200, 0, 4096, true));
    uint8_t out[100] = {0};
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_UINT(100, fake_lab_uart_write(out, 100));
    }
    TEST_ASSERT_EQUAL_UINT(44, fake_lab_uart_dropped());
    fake_lab_time_us(100000);
    uint8_t in[300];
    TEST_ASSERT_EQUAL_UINT(256, uart_read(in, 300, NULL, NULL, NULL));
}

static void test_an_echo_peer_matches_within_3_percent_and_garbles_beyond(void)
{
    uint8_t out[256];
    for (size_t i = 0; i < sizeof out; i++) {
        out[i] = (uint8_t)i;
    }
    uint8_t in[256];
    uint32_t fe = 0;
    uint32_t pe = 0;
    uint32_t ov = 0;
    /* the same settings: back unchanged */
    fake_lab_uart_peer(FAKE_LAB_UART_ECHO, 115200, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(115200, 0, 4096, true));
    TEST_ASSERT_EQUAL_UINT(100, fake_lab_uart_write(out, 100));
    fake_lab_time_us(100000);
    TEST_ASSERT_EQUAL_UINT(100, uart_read(in, sizeof in, &fe, &pe, &ov));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(out, in, 100);
    TEST_ASSERT_EQUAL_UINT32(0, fe + pe + ov);
    fake_lab_uart_close();
    /* 3 % off is the edge of what it takes; 4 % is garbage, with a framing error for each */
    fake_lab_uart_peer(FAKE_LAB_UART_ECHO, 118656, 0); /* +3 % */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(115200, 0, 4096, true));
    TEST_ASSERT_EQUAL_UINT(50, fake_lab_uart_write(out, 50));
    fake_lab_time_us(200000);
    TEST_ASSERT_EQUAL_UINT(50, uart_read(in, sizeof in, &fe, &pe, &ov));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(out, in, 50);
    TEST_ASSERT_EQUAL_UINT32(0, fe + pe + ov);
    fake_lab_uart_close();
    fake_lab_uart_peer(FAKE_LAB_UART_ECHO, 119808, 0); /* +4 % */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(115200, 0, 4096, true));
    TEST_ASSERT_EQUAL_UINT(256, fake_lab_uart_write(out, 256));
    fake_lab_time_us(400000);
    TEST_ASSERT_EQUAL_UINT(256, uart_read(in, sizeof in, &fe, &pe, &ov));
    TEST_ASSERT_EQUAL_UINT32(256, fe);
    TEST_ASSERT_EQUAL_UINT32(0, pe);
    for (size_t i = 0; i < 256; i++) {
        TEST_ASSERT_NOT_EQUAL(out[i], in[i]); /* no byte survives a mismatched peer */
    }
    fake_lab_uart_close();
    /* the peer slower than the line: the echo is later than the writes by its own pace */
    fake_lab_uart_peer(FAKE_LAB_UART_ECHO, 9600, 0);
    fe = pe = ov = 0;
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(115200, 0, 4096, true));
    fake_lab_time_us(500000);
    TEST_ASSERT_EQUAL_UINT(8, fake_lab_uart_write(out, 8));
    fake_lab_time_us(500000 + 800 + 3000);
    TEST_ASSERT_EQUAL_UINT(3, uart_read(in, sizeof in, &fe, &pe, &ov));
}

static void test_a_format_mismatch_is_a_parity_error_where_parity_is_checked(void)
{
    uint8_t out[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t in[8];
    uint32_t fe = 0;
    uint32_t pe = 0;
    uint32_t ov = 0;
    fake_lab_uart_peer(FAKE_LAB_UART_ECHO, 115200, 1);                     /* 8E1 */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(115200, 0, 256, true)); /* 8N1 */
    TEST_ASSERT_EQUAL_UINT(8, fake_lab_uart_write(out, 8));
    fake_lab_time_us(100000);
    TEST_ASSERT_EQUAL_UINT(8, uart_read(in, sizeof in, &fe, &pe, &ov));
    TEST_ASSERT_EQUAL_UINT32(8, fe); /* 8E1 sent to a receiver that checks none: the peer is the */
    TEST_ASSERT_EQUAL_UINT32(0, pe); /* receiver of the first leg, the tool of the second */
    fake_lab_uart_close();
    fake_lab_uart_peer(FAKE_LAB_UART_ECHO, 115200, 0); /* 8N1 */
    fe = pe = 0;
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(115200, 1, 256, true)); /* 8E1 */
    TEST_ASSERT_EQUAL_UINT(8, fake_lab_uart_write(out, 8));
    fake_lab_time_us(200000);
    TEST_ASSERT_EQUAL_UINT(8, uart_read(in, sizeof in, &fe, &pe, &ov));
    TEST_ASSERT_EQUAL_UINT32(8, pe); /* the tool checks parity on what the 8N1 peer sends back */
}

static void test_the_scenes(void)
{
    TEST_ASSERT_EQUAL_UINT(1300, fake_lab_uart_scene_len(FAKE_LAB_UART_TEXT));
    TEST_ASSERT_EQUAL_UINT(1024, fake_lab_uart_scene_len(FAKE_LAB_UART_BINARY));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_uart_scene_len(FAKE_LAB_UART_SILENT));
    const char *first = "stark uart scene line 00\r\n";
    for (size_t j = 0; j < 26; j++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)first[j], fake_lab_uart_scene_byte(FAKE_LAB_UART_TEXT, j));
    }
    TEST_ASSERT_EQUAL_UINT8('4', fake_lab_uart_scene_byte(FAKE_LAB_UART_TEXT, 49 * 26 + 22));
    TEST_ASSERT_EQUAL_UINT8('9', fake_lab_uart_scene_byte(FAKE_LAB_UART_TEXT, 49 * 26 + 23));
    TEST_ASSERT_EQUAL_UINT8(3, fake_lab_uart_scene_byte(FAKE_LAB_UART_BINARY, 0));
    TEST_ASSERT_EQUAL_UINT8(10, fake_lab_uart_scene_byte(FAKE_LAB_UART_BINARY, 1));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(1023u * 7u + 3u),
                            fake_lab_uart_scene_byte(FAKE_LAB_UART_BINARY, 1023));
}

static void test_a_talking_peer_says_its_scene_once_at_its_own_pace(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 9600, 0);
    fake_lab_time_us(5000);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(9600, 0, 4096, false));
    uint8_t in[64];
    uint32_t fe = 0;
    uint32_t pe = 0;
    uint32_t ov = 0;
    TEST_ASSERT_EQUAL_UINT(0, uart_read(in, sizeof in, &fe, &pe, &ov));
    fake_lab_time_us(5000 + 10000); /* 10 ms: nine characters */
    TEST_ASSERT_EQUAL_UINT(9, uart_read(in, sizeof in, &fe, &pe, &ov));
    TEST_ASSERT_EQUAL_UINT8_ARRAY("stark uar", in, 9);
    fake_lab_time_us(5000 + 10000000); /* long after the end of the scene */
    uint32_t total = 9;
    size_t n;
    while ((n = uart_read(in, sizeof in, &fe, &pe, &ov)) > 0u) {
        total += (uint32_t)n;
    }
    TEST_ASSERT_EQUAL_UINT32(1300, total);
    TEST_ASSERT_EQUAL_UINT(0, uart_read(in, sizeof in, &fe, &pe, &ov)); /* once, then silence */
    TEST_ASSERT_EQUAL_UINT32(0, fe + pe + ov);
    /* a binary scene, with a peer at another baud: framing errors, counted */
    fake_lab_uart_close();
    fake_lab_uart_peer(FAKE_LAB_UART_BINARY, 19200, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(9600, 0, 4096, false));
    fake_lab_time_us(5000 + 10000000 + 100000);
    TEST_ASSERT_EQUAL_UINT(64, uart_read(in, sizeof in, &fe, &pe, NULL));
    TEST_ASSERT_EQUAL_UINT32(64, fe);
    TEST_ASSERT_EQUAL_UINT(64,
                           uart_read(in, sizeof in, NULL, NULL, NULL)); /* NULL counters are fine */
    fake_lab_uart_close();
    /* the parity flag of a format mismatch with a checking receiver */
    fake_lab_uart_peer(FAKE_LAB_UART_BINARY, 9600, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(9600, 1, 4096, false));
    fake_lab_time_us(5000 + 10000000 + 100000 + 100000);
    fe = pe = 0;
    TEST_ASSERT_TRUE(uart_read(in, sizeof in, &fe, &pe, NULL) > 0u);
    TEST_ASSERT_TRUE(pe > 0u);
    TEST_ASSERT_EQUAL_UINT32(0, fe);
}

static void
test_a_stream_that_outruns_the_receive_buffer_loses_its_oldest_and_counts_an_overrun(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 921600, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(921600, 0, 256, false));
    fake_lab_time_us(100000); /* the whole scene came, nobody read */
    uint8_t in[300];
    uint32_t ov = 0;
    TEST_ASSERT_EQUAL_UINT(256, uart_read(in, sizeof in, NULL, NULL, &ov));
    TEST_ASSERT_EQUAL_UINT32(1, ov);
    TEST_ASSERT_EQUAL_UINT8(fake_lab_uart_scene_byte(FAKE_LAB_UART_TEXT, 1300 - 256), in[0]);
    TEST_ASSERT_EQUAL_UINT8('\n', in[255]);
    TEST_ASSERT_EQUAL_UINT(0, uart_read(in, sizeof in, NULL, NULL, &ov));
    TEST_ASSERT_EQUAL_UINT32(1, ov);
    fake_lab_uart_close();
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(921600, 0, 256, false));
    fake_lab_time_us(100000 + 100000);
    TEST_ASSERT_EQUAL_UINT(256, uart_read(in, sizeof in, NULL, NULL, NULL)); /* NULL overrun */
}

static void test_the_rx_pad_shows_the_peers_stream_and_other_pads_do_not(void)
{
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 9600, 0);
    uint32_t ts[64];
    size_t n = 0;
    uint32_t seen = 0;
    bool first = false;
    fake_lab_time_us(1000);
    /* a short window: the stream ends by the clock, not by the ring */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_edges(39, 10000, ts, 64, &n, &seen, &first));
    TEST_ASSERT_TRUE(first);
    TEST_ASSERT_TRUE(n > 10u && n < 64u);
    TEST_ASSERT_EQUAL_UINT32(n, seen);
    TEST_ASSERT_EQUAL_UINT32(1000u * 240u,
                             ts[0]); /* the first start bit falls at the capture's start */
    /* the TX pad and any other pad are not the peer's line */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_edges(38, 10000, ts, 64, &n, &seen, &first));
    TEST_ASSERT_EQUAL_UINT(0, n);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_edges(5, 10000, ts, 64, &n, &seen, &first));
    TEST_ASSERT_EQUAL_UINT(0, n);
    /* a peer with no baud has nothing to say */
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 0, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_edges(39, 10000, ts, 64, &n, &seen, &first));
    TEST_ASSERT_EQUAL_UINT(0, n);
    TEST_ASSERT_TRUE(first);
    /* a peer that talks only to the tool, not on the RX pad until it is told which pad that is */
    fake_lab_reset();
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 9600, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_edges(39, 10000, ts, 64, &n, &seen, &first));
    TEST_ASSERT_EQUAL_UINT(0, n);
    /* the reset forgets the pins, the peer and an open session */
    fake_lab_uart_pins(38, 39);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_uart_open(9600, 0, 256, true));
    fake_lab_reset();
    TEST_ASSERT_FALSE(fake_lab_uart_is_open());
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_edges(39, 10000, ts, 64, &n, &seen, &first));
    TEST_ASSERT_EQUAL_UINT(0, n);
}

/* ---- the SPI devices (STARK-0706) -------------------------------------------------------------
 */

static void test_every_spi_device_vector_matches_the_independent_reference(void)
{
    TEST_ASSERT_TRUE(FAKE_LAB_SPI_DEV_VECS >= 50u);
    for (size_t i = 0; i < FAKE_LAB_SPI_DEV_VECS; i++) {
        const fake_lab_spi_dev_vec_t *v = &fake_lab_spi_dev_vecs[i];
        uint8_t rx[32];
        memset(rx, 0xEE, sizeof rx);
        fake_lab_spi_device((fake_lab_spi_mode_t)v->device);
        TEST_ASSERT_EQUAL(STARK_OK, fake_lab_spi_xfer(0, 1000000u, 0, v->tx, rx, v->n));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(v->rx, rx, v->n);
        if (v->n < sizeof rx) {
            TEST_ASSERT_EQUAL_HEX8(0xEE, rx[v->n]); /* nothing written past n */
        }
    }
}

static void test_the_spi_model_is_empty_at_power_on_and_counts_its_transfers(void)
{
    uint8_t rx[4];
    const uint8_t tx[4] = {0x9f, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_spi_xfer(1, 250000u, 3, tx, rx, 4));
    TEST_ASSERT_EQUAL_HEX8(0, rx[1]); /* no device: MISO idles low */
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL_UINT8(1, fake_lab_spi_last_slot());
    TEST_ASSERT_EQUAL_UINT32(250000u, fake_lab_spi_last_hz());
    TEST_ASSERT_EQUAL_UINT8(3, fake_lab_spi_last_mode());
    TEST_ASSERT_EQUAL_UINT(4, fake_lab_spi_last_len());
    fake_lab_spi_device(FAKE_LAB_SPI_FLASH);
    fake_lab_spi_device(FAKE_LAB_SPI_MODES); /* past the list: ignored */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_spi_xfer(0, 1000000u, 0, tx, rx, 4));
    TEST_ASSERT_EQUAL_HEX8(0xef, rx[1]);
    fake_lab_reset();
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_spi_xfer(0, 1000000u, 0, tx, rx, 4));
    TEST_ASSERT_EQUAL_HEX8(0, rx[1]); /* the reset forgot the flash */
}

static void test_the_spi_faults_count_down(void)
{
    uint8_t rx[4];
    const uint8_t tx[4] = {0x9f, 0, 0, 0};
    fake_lab_spi_device(FAKE_LAB_SPI_FLASH);
    fake_lab_fault(FAKE_LAB_FAULT_SPI_ERROR, -1, 2);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_lab_spi_xfer(0, 1000000u, 0, tx, rx, 4));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_lab_spi_xfer(0, 1000000u, 0, tx, rx, 4));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_spi_xfer(0, 1000000u, 0, tx, rx, 4));
    TEST_ASSERT_EQUAL_HEX8(0xef, rx[1]);
    TEST_ASSERT_EQUAL_UINT(3, fake_lab_spi_calls()); /* a failed transfer still reached the model */
    fake_lab_fault(FAKE_LAB_FAULT_SPI_NO_ANSWER, -1, -1);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_spi_xfer(0, 1000000u, 0, tx, rx, 4));
    TEST_ASSERT_EQUAL_HEX8(0xff, rx[0]);
    TEST_ASSERT_EQUAL_HEX8(0xff, rx[3]);
    fake_lab_fault(FAKE_LAB_FAULT_NONE, -1, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_spi_xfer(0, 1000000u, 0, tx, rx, 4));
    TEST_ASSERT_EQUAL_HEX8(0xef, rx[1]);
}

static stark_err_t spi_chip(uint8_t slot, const uint8_t *tx, uint8_t *rx, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        rx[i] = (uint8_t)(tx[i] ^ (0x40u + slot));
    }
    return STARK_OK;
}

static void test_a_bound_spi_chip_answers_instead_of_the_mode_and_faults_come_first(void)
{
    uint8_t rx[2];
    const uint8_t tx[2] = {0x10, 0x20};
    fake_lab_spi_device(FAKE_LAB_SPI_ECHO);
    fake_lab_spi_ext(spi_chip);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_spi_xfer(0, 1000000u, 0, tx, rx, 2));
    TEST_ASSERT_EQUAL_HEX8(0x50, rx[0]);
    TEST_ASSERT_EQUAL_HEX8(0x60, rx[1]);
    fake_lab_fault(FAKE_LAB_FAULT_SPI_ERROR, -1, 1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_lab_spi_xfer(0, 1000000u, 0, tx, rx, 2));
    fake_lab_spi_ext(NULL);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_spi_xfer(0, 1000000u, 0, tx, rx, 2));
    TEST_ASSERT_EQUAL_HEX8(0x00, rx[0]); /* the echo again */
    TEST_ASSERT_EQUAL_HEX8(0x10, rx[1]);
}

static void test_spi_bad_arguments_touch_nothing(void)
{
    uint8_t rx[33];
    const uint8_t tx[33] = {0};
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_spi_xfer(0, 1000000u, 0, NULL, rx, 4));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_spi_xfer(0, 1000000u, 0, tx, NULL, 4));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_spi_xfer(0, 1000000u, 0, tx, rx, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_spi_xfer(0, 1000000u, 0, tx, rx, 33));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_spi_xfer(0, 1000000u, 0, tx, rx, 32));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_power_on_pads_are_hiz_and_read_low);
    RUN_TEST(test_input_follows_its_pull);
    RUN_TEST(test_output_reads_back_its_level_and_hiz_releases_it);
    RUN_TEST(test_an_external_source_wins_on_an_input_and_fights_an_output);
    RUN_TEST(test_square_wave_follows_the_injected_clock);
    RUN_TEST(test_a_wire_carries_an_output_to_another_pad);
    RUN_TEST(test_wire_limits_and_precedence);
    RUN_TEST(test_stuck_faults_count_down_and_filter_by_pin);
    RUN_TEST(test_config_error_fails_pad_in_and_out_but_never_hiz);
    RUN_TEST(test_bad_arguments_touch_nothing);
    RUN_TEST(test_reset_returns_to_power_on);
    RUN_TEST(test_a_pwm_pad_reads_its_waveform_and_hiz_ends_it);
    RUN_TEST(test_a_wire_carries_a_pwm_and_an_external_source_fights_it);
    RUN_TEST(test_the_square_wave_formula);
    RUN_TEST(test_a_pulse_rests_at_the_opposite_level_and_is_counted);
    RUN_TEST(test_pwm_and_pulse_obey_the_config_fault_and_bad_arguments);
    RUN_TEST(test_a_steady_pad_has_no_edge_and_its_level_is_the_first_level);
    RUN_TEST(test_a_square_source_gives_its_edges_at_240_mhz);
    RUN_TEST(test_the_ring_is_bounded_and_the_count_saturates);
    RUN_TEST(test_a_pwm_pad_and_its_wire_are_square_sources);
    RUN_TEST(test_a_degenerate_wave_is_steady);
    RUN_TEST(test_jitter_is_bounded_deterministic_and_keeps_order);
    RUN_TEST(test_glitches_are_half_microsecond_pairs_where_there_is_room);
    RUN_TEST(test_the_epoch_puts_a_window_across_the_counter_wrap);
    RUN_TEST(test_edge_faults_and_bad_arguments);
    RUN_TEST(test_the_bus_has_the_five_devices_and_nothing_else_at_power_on);
    RUN_TEST(test_each_device_answers_as_documented);
    RUN_TEST(test_the_register_files_store_auto_increment_and_wrap);
    RUN_TEST(test_other_addresses_belong_to_the_bound_fakes);
    RUN_TEST(test_i2c_faults);
    RUN_TEST(test_a_command_write_to_the_light_sensor_counts_and_a_pointer_alone_does_not);
    RUN_TEST(test_the_uart_bits_per_character);
    RUN_TEST(test_the_uart_opens_closes_and_refuses_bad_settings);
    RUN_TEST(test_a_loopback_returns_each_character_after_it_crossed_the_wire);
    RUN_TEST(test_the_loopback_queue_is_bounded_and_counts_what_it_drops);
    RUN_TEST(test_an_echo_peer_matches_within_3_percent_and_garbles_beyond);
    RUN_TEST(test_a_format_mismatch_is_a_parity_error_where_parity_is_checked);
    RUN_TEST(test_the_scenes);
    RUN_TEST(test_a_talking_peer_says_its_scene_once_at_its_own_pace);
    RUN_TEST(test_a_stream_that_outruns_the_receive_buffer_loses_its_oldest_and_counts_an_overrun);
    RUN_TEST(test_the_rx_pad_shows_the_peers_stream_and_other_pads_do_not);
    RUN_TEST(test_every_spi_device_vector_matches_the_independent_reference);
    RUN_TEST(test_the_spi_model_is_empty_at_power_on_and_counts_its_transfers);
    RUN_TEST(test_the_spi_faults_count_down);
    RUN_TEST(test_a_bound_spi_chip_answers_instead_of_the_mode_and_faults_come_first);
    RUN_TEST(test_spi_bad_arguments_touch_nothing);
    return UNITY_END();
}
