/*
 * test_fake_lab.c — the test-only pad model (TASKS.md STARK-0701, ADR-0023 §2): hi-Z, input
 * with a pull, output with a level, external sources, wires and faults. The model carries the
 * evidence of every host and emulator test that uses it, so it is tested on its own first.
 */
#include "fake_lab.h"
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
    return UNITY_END();
}
