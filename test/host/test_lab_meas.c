/*
 * test_lab_meas.c — the frequency and duty arithmetic (components/stark_lab/lab_meas.c, TASKS.md
 * STARK-0703, ADR-0023 §4) against the edge sets of scripts/gen_lab_vectors.py: square waves
 * from 1 Hz to 50 kHz at six duties with seeded jitter, glitch trains, a steady line, an
 * overflow, a counter wrap and hostile input, each with the result an independent Python
 * implementation computes. Every vector is also held to the accuracy ADR-0023 §4 allows of the
 * wave it was made from. Proves the arithmetic and its bounds, never a signal on a pad
 * (ROADMAP V1 exit 13).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fake_lab_vectors.h"
#include "lab_meas.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static uint32_t absdiff(uint32_t a, uint32_t b)
{
    return a > b ? a - b : b - a;
}

/* AC 1: every vector reproduces the reference exactly, and the wave within the stated accuracy. */
static void test_every_vector_matches_the_independent_reference(void)
{
    TEST_ASSERT_TRUE(FAKE_LAB_VECS >= 60u);
    size_t measured = 0;
    for (size_t i = 0; i < FAKE_LAB_VECS; i++) {
        const fake_lab_vec_t *v = &fake_lab_vecs[i];
        lab_meas_t r =
            lab_meas_compute(v->ts, v->n, v->first_level != 0u, v->tick_hz, v->overflowed != 0u);
        char msg[96];
        snprintf(msg, sizeof msg, "vector %s", v->name);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(v->status, (unsigned)r.status, msg);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->hz_x100, r.hz_x100, msg);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(v->duty_x10, r.duty_x10, msg);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->period_ticks, r.period_ticks, msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(v->steady_high, r.steady_high ? 1u : 0u, msg);
        if (v->nominal_hz_x100 != 0u) {
            measured++;
            TEST_ASSERT_EQUAL_UINT_MESSAGE(STARK_LAB_MEAS_OK, (unsigned)r.status, msg);
            uint64_t ppm =
                (uint64_t)absdiff(r.hz_x100, v->nominal_hz_x100) * 1000000u / v->nominal_hz_x100;
            TEST_ASSERT_TRUE_MESSAGE(ppm <= v->tol_hz_ppm, msg);
            TEST_ASSERT_TRUE_MESSAGE(absdiff(r.duty_x10, v->nominal_duty_x10) <= v->tol_duty_x10,
                                     msg);
            /* the stated bands themselves, restated: 0.5 % to 10 kHz and 2 % above; 2 and 5
             * points of duty */
            bool slow = v->nominal_hz_x100 <= 1000000u;
            TEST_ASSERT_TRUE_MESSAGE(ppm <= (slow ? 5000u : 20000u), msg);
            TEST_ASSERT_TRUE_MESSAGE(absdiff(r.duty_x10, v->nominal_duty_x10) <= (slow ? 20u : 50u),
                                     msg);
        }
    }
    TEST_ASSERT_TRUE(measured >= 45u);
}

static void test_the_catalogue_covers_every_status(void)
{
    unsigned seen[7] = {0};
    for (size_t i = 0; i < FAKE_LAB_VECS; i++) {
        seen[fake_lab_vecs[i].status]++;
    }
    for (size_t s = 0; s < 7; s++) {
        TEST_ASSERT_TRUE_MESSAGE(seen[s] > 0u, "a status has no vector");
    }
}

static void test_the_window_plan_matches_the_reference(void)
{
    for (size_t i = 0; i < FAKE_LAB_PLAN_VECS; i++) {
        const fake_lab_plan_vec_t *v = &fake_lab_plan_vecs[i];
        lab_meas_plan_t p = lab_meas_plan(v->gate_edges);
        char msg[48];
        snprintf(msg, sizeof msg, "gate edges %u", (unsigned)v->gate_edges);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(v->too_fast, p.too_fast ? 1u : 0u, msg);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->window_ms, p.window_ms, msg);
    }
}

static void test_the_window_always_fits_the_ring(void)
{
    /* the expected edges of a window sized from any gate count that is not too fast */
    for (uint32_t c = 1; c <= 4096u; c++) {
        lab_meas_plan_t p = lab_meas_plan(c);
        if (p.too_fast) {
            TEST_ASSERT_TRUE(c > 2002u);
            continue;
        }
        TEST_ASSERT_TRUE(p.window_ms >= LAB_MEAS_WINDOW_MS_MIN);
        TEST_ASSERT_TRUE(p.window_ms <= LAB_MEAS_WINDOW_MS_MAX);
        uint64_t expect = (uint64_t)c * p.window_ms / LAB_MEAS_GATE_MS;
        TEST_ASSERT_TRUE_MESSAGE(expect <= 1010u, "the window would overflow the ring");
    }
    TEST_ASSERT_EQUAL_UINT32(LAB_MEAS_WINDOW_MS_MAX, lab_meas_plan(0).window_ms);
    TEST_ASSERT_FALSE(lab_meas_plan(0).too_fast);
}

static lab_meas_t square(uint32_t period_ticks, uint32_t high_ticks, size_t edges, bool first)
{
    static uint32_t ts[1100];
    uint32_t t = 100000;
    bool level = first;
    for (size_t i = 0; i < edges; i++) {
        /* edges alternate: the step after a rising edge is the high time */
        uint32_t step = level ? period_ticks - high_ticks : high_ticks;
        ts[i] = t;
        t += step;
        level = !level;
    }
    return lab_meas_compute(ts, edges, first, 240000000u, false);
}

static void test_a_known_wave_gives_its_numbers(void)
{
    /* 1 kHz at 25 %: 240000 ticks, 60000 of them high, the first edge rising */
    lab_meas_t r = square(240000u, 60000u, 100, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_OK, r.status);
    TEST_ASSERT_EQUAL_UINT32(100000u, r.hz_x100);
    TEST_ASSERT_EQUAL_UINT16(250u, r.duty_x10);
    TEST_ASSERT_EQUAL_UINT32(240000u, r.period_ticks);
    TEST_ASSERT_EQUAL_UINT32(100u, r.edges);
    /* the same wave read from the other level: the first edge is a falling one */
    r = square(240000u, 60000u, 101, true);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_OK, r.status);
    TEST_ASSERT_EQUAL_UINT32(100000u, r.hz_x100);
    TEST_ASSERT_EQUAL_UINT16(250u, r.duty_x10);
}

static void test_the_range_limits_are_inclusive_of_the_stated_guard(void)
{
    /* exactly 50 kHz is in range; 50 kHz + 0.1 % is the first thing that is too fast */
    lab_meas_t r = square(4800u, 2400u, 400, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_OK, r.status);
    TEST_ASSERT_EQUAL_UINT32(5000000u, r.hz_x100);
    r = square(4795u, 2400u, 400, false); /* 50.052 kHz: above 50.05 */
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_TOO_FAST, r.status);
    r = square(4797u, 2400u, 400, false); /* 50.031 kHz: inside the guard */
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_OK, r.status);
    /* 1 Hz is in range, 0.98 Hz is not */
    r = square(240000000u, 120000000u, 10, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_OK, r.status);
    TEST_ASSERT_EQUAL_UINT32(100u, r.hz_x100);
    r = square(244897959u, 120000000u, 10, false); /* 0.98 Hz */
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_TOO_SLOW, r.status);
    TEST_ASSERT_EQUAL_UINT32(0u, r.hz_x100);
}

static void test_every_status_is_reported_and_never_as_a_number(void)
{
    static const uint32_t glitchy[] = {1000, 1100, 121000, 121100, 241000};
    static const uint32_t back[] = {1000, 241000, 120000, 480000};
    static const uint32_t good[] = {1000, 121000, 241000, 361000, 481000};
    lab_meas_t r;

    r = lab_meas_compute(NULL, 0, true, 240000000u, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_NO_SIGNAL, r.status);
    TEST_ASSERT_TRUE(r.steady_high);
    r = lab_meas_compute(NULL, 0, false, 240000000u, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_NO_SIGNAL, r.status);
    TEST_ASSERT_FALSE(r.steady_high);

    r = lab_meas_compute(good, 5, false, 240000000u, true);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_OVERFLOW, r.status);
    TEST_ASSERT_EQUAL_UINT32(0u, r.hz_x100);
    r = lab_meas_compute(good, 5000, false, 240000000u, true); /* the count is clamped */
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_OVERFLOW, r.status);
    TEST_ASSERT_EQUAL_UINT32(STARK_LAB_MEAS_RING, r.edges);

    r = lab_meas_compute(glitchy, 5, false, 240000000u, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_GLITCH, r.status);
    TEST_ASSERT_EQUAL_UINT32(0u, r.hz_x100);
    r = lab_meas_compute(back, 4, false, 240000000u, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_INVALID, r.status);

    /* fewer than two rising edges: one edge, two edges, three edges starting high */
    r = lab_meas_compute(good, 1, false, 240000000u, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_TOO_SLOW, r.status);
    r = lab_meas_compute(good, 2, false, 240000000u, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_TOO_SLOW, r.status);
    r = lab_meas_compute(good, 3, true, 240000000u, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_TOO_SLOW, r.status);
    /* ... and the smallest window that has two: rise, fall, rise */
    r = lab_meas_compute(good, 3, false, 240000000u, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_OK, r.status);
    TEST_ASSERT_EQUAL_UINT16(500u, r.duty_x10);
}

static void test_hostile_input_is_refused_without_a_read_past_the_ring(void)
{
    static uint32_t big[STARK_LAB_MEAS_RING + 8];
    lab_meas_t r = lab_meas_compute(big, STARK_LAB_MEAS_RING + 1u, false, 240000000u, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_INVALID, r.status);
    r = lab_meas_compute(NULL, 4, false, 240000000u, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_INVALID, r.status);
    r = lab_meas_compute(big, 4, false, 0u, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_INVALID, r.status);
    r = lab_meas_compute(big, 0, false, 0u, true); /* a zero clock beats everything */
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_INVALID, r.status);
    /* duplicate and zero stamps are glitches, never a division by zero */
    memset(big, 0, sizeof big);
    r = lab_meas_compute(big, 100, false, 240000000u, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_GLITCH, r.status);
    r = lab_meas_compute(big, 1, false, 240000000u, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_TOO_SLOW, r.status);
}

static void test_a_counter_wrap_inside_the_window_is_harmless(void)
{
    uint32_t ts[64];
    uint32_t t = 0xFFFFFFFFu - 1000000u; /* the counter wraps after the first few edges */
    for (size_t i = 0; i < 64; i++) {
        ts[i] = t;
        t += 120000u; /* 1 kHz at 50 %: every step is half a period */
    }
    TEST_ASSERT_TRUE(ts[63] < ts[0]); /* it did wrap */
    lab_meas_t r = lab_meas_compute(ts, 64, false, 240000000u, false);
    TEST_ASSERT_EQUAL(STARK_LAB_MEAS_OK, r.status);
    TEST_ASSERT_EQUAL_UINT32(100000u, r.hz_x100);
    TEST_ASSERT_EQUAL_UINT16(500u, r.duty_x10);
}

/* xorshift32: the same sequence everywhere */
static uint32_t s_rng = 0x2545F491u;
static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* AC 2: whatever the input, a number comes out only with status OK and inside the range. */
static void test_ten_thousand_random_edge_sets_never_return_a_value_outside_their_status(void)
{
    static uint32_t ts[STARK_LAB_MEAS_RING + 4];
    unsigned by_status[7] = {0};
    for (int round = 0; round < 10000; round++) {
        size_t n = rnd() % (STARK_LAB_MEAS_RING + 1u);
        uint32_t t = rnd();
        uint32_t scale = 1u << (rnd() % 24u);
        for (size_t i = 0; i < n; i++) {
            ts[i] = t;
            uint32_t step = rnd() % (scale + 1u);
            if (rnd() % 64u == 0u) {
                step = rnd(); /* a wild step: backwards or huge */
            }
            t += step;
        }
        uint32_t tick = rnd() % 8u == 0u ? 0u : 1u + rnd() % 480000000u;
        bool over = rnd() % 16u == 0u;
        lab_meas_t r = lab_meas_compute(ts, n, (rnd() & 1u) != 0u, tick, over);
        TEST_ASSERT_TRUE((unsigned)r.status <= (unsigned)STARK_LAB_MEAS_INVALID);
        TEST_ASSERT_TRUE(r.edges <= STARK_LAB_MEAS_RING);
        by_status[r.status]++;
        if (r.status == STARK_LAB_MEAS_OK) {
            TEST_ASSERT_TRUE(r.hz_x100 >= LAB_MEAS_HZ_X100_MIN &&
                             r.hz_x100 <= LAB_MEAS_HZ_X100_MAX);
            TEST_ASSERT_TRUE(r.duty_x10 <= 1000u);
            TEST_ASSERT_TRUE(r.period_ticks > 0u);
        } else {
            TEST_ASSERT_EQUAL_UINT32(0u, r.hz_x100);
            TEST_ASSERT_EQUAL_UINT16(0u, r.duty_x10);
            TEST_ASSERT_EQUAL_UINT32(0u, r.period_ticks);
        }
    }
    /* the generator reaches the interesting statuses, so the loop proves something */
    TEST_ASSERT_TRUE(by_status[STARK_LAB_MEAS_OK] > 0u);
    TEST_ASSERT_TRUE(by_status[STARK_LAB_MEAS_INVALID] > 0u);
    TEST_ASSERT_TRUE(by_status[STARK_LAB_MEAS_OVERFLOW] > 0u);
    TEST_ASSERT_TRUE(by_status[STARK_LAB_MEAS_GLITCH] > 0u);
}

static void test_the_same_input_gives_the_same_result(void)
{
    const fake_lab_vec_t *v = &fake_lab_vecs[20];
    lab_meas_t a = lab_meas_compute(v->ts, v->n, v->first_level != 0u, v->tick_hz, false);
    lab_meas_t b = lab_meas_compute(v->ts, v->n, v->first_level != 0u, v->tick_hz, false);
    TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

static void test_the_texts(void)
{
    char buf[48];
    lab_meas_t m = {.status = STARK_LAB_MEAS_OK, .hz_x100 = 100000u, .duty_x10 = 250u};
    stark_lab_meas_text(&m, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("1.000 kHz duty 25.0 %", buf);
    stark_lab_meas_log_text(&m, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("1000.0 Hz duty 25.0 %", buf);

    m.hz_x100 = 4000000u;
    m.duty_x10 = 500u;
    stark_lab_meas_text(&m, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("40.000 kHz duty 50.0 %", buf);
    stark_lab_meas_log_text(&m, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("40000.0 Hz duty 50.0 %", buf);

    m.hz_x100 = 99999u; /* 999.99 Hz: the log rounds to a tenth and carries */
    m.duty_x10 = 999u;
    stark_lab_meas_text(&m, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("999.99 Hz duty 99.9 %", buf);
    stark_lab_meas_log_text(&m, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("1000.0 Hz duty 99.9 %", buf);

    m.hz_x100 = 100u;
    m.duty_x10 = 10u;
    stark_lab_meas_text(&m, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("1.00 Hz duty 1.0 %", buf);
    stark_lab_meas_log_text(&m, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("1.0 Hz duty 1.0 %", buf);

    static const struct {
        stark_lab_meas_status_t st;
        bool high;
        const char *ui;
        const char *log;
    } k[] = {
        {STARK_LAB_MEAS_TOO_FAST, false, "> 50 kHz, not measured", "too fast"},
        {STARK_LAB_MEAS_TOO_SLOW, false, "< 1 Hz", "too slow"},
        {STARK_LAB_MEAS_NO_SIGNAL, true, "no signal (high)", "no signal (high)"},
        {STARK_LAB_MEAS_NO_SIGNAL, false, "no signal (low)", "no signal (low)"},
        {STARK_LAB_MEAS_OVERFLOW, false, "overflow: too many edges", "overflow"},
        {STARK_LAB_MEAS_GLITCH, false, "glitches (< 1 us)", "glitch"},
        {STARK_LAB_MEAS_INVALID, false, "invalid timestamps", "invalid"},
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        lab_meas_t r = {.status = k[i].st, .steady_high = k[i].high};
        stark_lab_meas_text(&r, buf, sizeof buf);
        TEST_ASSERT_EQUAL_STRING(k[i].ui, buf);
        stark_lab_meas_log_text(&r, buf, sizeof buf);
        TEST_ASSERT_EQUAL_STRING(k[i].log, buf);
    }
    lab_meas_t bad = {.status = (stark_lab_meas_status_t)99};
    stark_lab_meas_text(&bad, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("invalid timestamps", buf);
    stark_lab_meas_log_text(&bad, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("invalid", buf);
    /* the longest text fits the buffers the callers use (24 and 40) */
    stark_lab_meas_text(&(lab_meas_t){.status = STARK_LAB_MEAS_OVERFLOW}, buf, 40);
    TEST_ASSERT_TRUE(strlen(buf) < 40u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_vector_matches_the_independent_reference);
    RUN_TEST(test_the_catalogue_covers_every_status);
    RUN_TEST(test_the_window_plan_matches_the_reference);
    RUN_TEST(test_the_window_always_fits_the_ring);
    RUN_TEST(test_a_known_wave_gives_its_numbers);
    RUN_TEST(test_the_range_limits_are_inclusive_of_the_stated_guard);
    RUN_TEST(test_every_status_is_reported_and_never_as_a_number);
    RUN_TEST(test_hostile_input_is_refused_without_a_read_past_the_ring);
    RUN_TEST(test_a_counter_wrap_inside_the_window_is_harmless);
    RUN_TEST(test_ten_thousand_random_edge_sets_never_return_a_value_outside_their_status);
    RUN_TEST(test_the_same_input_gives_the_same_result);
    RUN_TEST(test_the_texts);
    return UNITY_END();
}
