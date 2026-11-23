/*
 * test_subghz_txpolicy.c — the Sub-GHz transmit policy (TASKS.md STARK-0605, ADR-0022 §6): every
 * limit at its exact boundary, the order the refusals are reported in, the rolling window's
 * edge, the off-time rule, failed attempts charged in full, the boot lock, a clock that wraps
 * or steps back, a ledger that fills, and the transport's own limits (stark_subghz_tx_ok) that
 * the hardware and the test transport both enforce. The limits are constants, so there is
 * nothing here that sets one.
 */
#include <stdint.h>
#include <string.h>
#include "subghz_txpolicy.h"
#include "unity.h"

#define MHZ_433_92 433920000u

static subghz_txpolicy_t s_p;

void setUp(void)
{
    subghz_txpolicy_init(&s_p, 0);
}
void tearDown(void)
{}

static subghz_tx_plan_t plan_us(uint32_t span_us)
{
    return (subghz_tx_plan_t){MHZ_433_92, 100, span_us};
}

static subghz_tx_verdict_t check_ms(uint32_t now, uint32_t ms)
{
    subghz_tx_plan_t p = plan_us(ms * 1000u);
    return subghz_txpolicy_check(&s_p, now, &p, NULL);
}

/* The boot lock has passed and the ledger is empty. */
static void fresh_after_lock(void)
{
    subghz_txpolicy_init(&s_p, 0);
    subghz_txpolicy_tick(&s_p, SUBGHZ_TX_BOOT_LOCK_MS);
}

static void test_verdict_names(void)
{
    TEST_ASSERT_EQUAL_STRING("ok", subghz_tx_verdict_name(SUBGHZ_TX_OK));
    TEST_ASSERT_EQUAL_STRING("band", subghz_tx_verdict_name(SUBGHZ_TX_REFUSE_BAND));
    TEST_ASSERT_EQUAL_STRING("length", subghz_tx_verdict_name(SUBGHZ_TX_REFUSE_LENGTH));
    TEST_ASSERT_EQUAL_STRING("duration", subghz_tx_verdict_name(SUBGHZ_TX_REFUSE_DURATION));
    TEST_ASSERT_EQUAL_STRING("budget", subghz_tx_verdict_name(SUBGHZ_TX_REFUSE_BUDGET));
    TEST_ASSERT_EQUAL_STRING("offtime", subghz_tx_verdict_name(SUBGHZ_TX_REFUSE_OFFTIME));
    TEST_ASSERT_EQUAL_STRING("file", subghz_tx_verdict_name(SUBGHZ_TX_REFUSE_FILE));
    TEST_ASSERT_EQUAL_STRING("no module", subghz_tx_verdict_name(SUBGHZ_TX_REFUSE_NO_MODULE));
    TEST_ASSERT_EQUAL_STRING("busy", subghz_tx_verdict_name(SUBGHZ_TX_REFUSE_BUSY));
    TEST_ASSERT_EQUAL_STRING("?", subghz_tx_verdict_name(SUBGHZ_TX_VERDICT_COUNT));
    TEST_ASSERT_EQUAL_STRING("?", subghz_tx_verdict_name((subghz_tx_verdict_t)99));
}

static void test_the_limits_are_the_documented_constants(void)
{
    TEST_ASSERT_EQUAL_UINT32(2048u, SUBGHZ_TX_MAX_SYMBOLS);
    TEST_ASSERT_EQUAL_UINT32(2000000u, SUBGHZ_TX_MAX_US);
    TEST_ASSERT_EQUAL_UINT32(3600000u, SUBGHZ_TX_WINDOW_MS);
    TEST_ASSERT_EQUAL_UINT32(360000u, SUBGHZ_TX_BUDGET_MS);
    TEST_ASSERT_EQUAL_UINT32(9u, SUBGHZ_TX_OFF_FACTOR);
    TEST_ASSERT_EQUAL_UINT32(18000u, SUBGHZ_TX_BOOT_LOCK_MS);
    TEST_ASSERT_EQUAL_UINT32(433050000u, SUBGHZ_BAND_MIN_HZ);
    TEST_ASSERT_EQUAL_UINT32(434790000u, SUBGHZ_BAND_MAX_HZ);
}

static void test_the_span_rounds_up_to_a_millisecond(void)
{
    TEST_ASSERT_EQUAL_UINT32(1, subghz_tx_ms(0));
    TEST_ASSERT_EQUAL_UINT32(1, subghz_tx_ms(1));
    TEST_ASSERT_EQUAL_UINT32(1, subghz_tx_ms(1000));
    TEST_ASSERT_EQUAL_UINT32(2, subghz_tx_ms(1001));
    TEST_ASSERT_EQUAL_UINT32(52, subghz_tx_ms(51200));
    TEST_ASSERT_EQUAL_UINT32(202, subghz_tx_ms(201600));
    TEST_ASSERT_EQUAL_UINT32(2000, subghz_tx_ms(2000000));
}

static void test_the_band_edges(void)
{
    fresh_after_lock();
    const uint32_t t = SUBGHZ_TX_BOOT_LOCK_MS;
    subghz_tx_plan_t p = plan_us(50000);
    p.freq_hz = 433050000u;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, subghz_txpolicy_check(&s_p, t, &p, NULL));
    p.freq_hz = 433049999u;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BAND, subghz_txpolicy_check(&s_p, t, &p, NULL));
    p.freq_hz = 434790000u;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, subghz_txpolicy_check(&s_p, t, &p, NULL));
    p.freq_hz = 434790001u;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BAND, subghz_txpolicy_check(&s_p, t, &p, NULL));
    p.freq_hz = 0;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BAND, subghz_txpolicy_check(&s_p, t, &p, NULL));
    p.freq_hz = UINT32_MAX;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BAND, subghz_txpolicy_check(&s_p, t, &p, NULL));
    p.freq_hz = 868000000u; /* another band entirely */
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BAND, subghz_txpolicy_check(&s_p, t, &p, NULL));
}

static void test_the_length_edges(void)
{
    fresh_after_lock();
    const uint32_t t = SUBGHZ_TX_BOOT_LOCK_MS;
    subghz_tx_plan_t p = plan_us(50000);
    p.symbols = 0;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_LENGTH, subghz_txpolicy_check(&s_p, t, &p, NULL));
    p.symbols = 1;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, subghz_txpolicy_check(&s_p, t, &p, NULL));
    p.symbols = 2048;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, subghz_txpolicy_check(&s_p, t, &p, NULL));
    p.symbols = 2049;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_LENGTH, subghz_txpolicy_check(&s_p, t, &p, NULL));
    p.symbols = UINT32_MAX;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_LENGTH, subghz_txpolicy_check(&s_p, t, &p, NULL));
    p = plan_us(0); /* symbols but no signal at all */
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_LENGTH, subghz_txpolicy_check(&s_p, t, &p, NULL));
}

static void test_the_duration_edges(void)
{
    fresh_after_lock();
    const uint32_t t = SUBGHZ_TX_BOOT_LOCK_MS;
    subghz_tx_plan_t p = plan_us(2000000u);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, subghz_txpolicy_check(&s_p, t, &p, NULL));
    p = plan_us(2000001u);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_DURATION, subghz_txpolicy_check(&s_p, t, &p, NULL));
    p = plan_us(UINT32_MAX);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_DURATION, subghz_txpolicy_check(&s_p, t, &p, NULL));
    p = plan_us(1);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, subghz_txpolicy_check(&s_p, t, &p, NULL));
}

/* Each refusal is the first limit in the documented order, with every later one also broken. */
static void test_the_refusal_order(void)
{
    fresh_after_lock();
    const uint32_t t = SUBGHZ_TX_BOOT_LOCK_MS;
    /* make the budget and the off-time refuse too: 180 x 2 s used, the last one just ended */
    uint32_t now = t;
    for (int i = 0; i < 180; i++) {
        subghz_txpolicy_record(&s_p, now, now + 2000, 2000);
        now += 2000;
        if (i < 179) {
            now += 18000;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(0, subghz_txpolicy_budget_left_ms(&s_p, now));
    subghz_tx_plan_t p = {100, 5000, 3000000u}; /* band, length and duration broken as well */
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BAND, subghz_txpolicy_check(&s_p, now, &p, NULL));
    p.freq_hz = MHZ_433_92;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_LENGTH, subghz_txpolicy_check(&s_p, now, &p, NULL));
    p.symbols = 100;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_DURATION, subghz_txpolicy_check(&s_p, now, &p, NULL));
    p.span_us = 1000000u;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BUDGET, subghz_txpolicy_check(&s_p, now, &p, NULL));
    /* with budget back (the whole window gone) only the off-time of a fresh record remains */
    now += SUBGHZ_TX_WINDOW_MS;
    subghz_txpolicy_record(&s_p, now, now + 1000, 1000);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, subghz_txpolicy_check(&s_p, now + 1000, &p, NULL));
}

static void test_the_boot_lock_is_the_longest_off_time(void)
{
    subghz_txpolicy_init(&s_p, 5000);
    subghz_tx_plan_t p = plan_us(1000);
    subghz_tx_info_t info;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, subghz_txpolicy_check(&s_p, 5000, &p, &info));
    TEST_ASSERT_EQUAL_UINT32(18000, info.wait_ms);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, subghz_txpolicy_check(&s_p, 22999, &p, &info));
    TEST_ASSERT_EQUAL_UINT32(1, info.wait_ms);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, subghz_txpolicy_check(&s_p, 23000, &p, &info));
    TEST_ASSERT_EQUAL_UINT32(0, info.wait_ms);
    TEST_ASSERT_EQUAL_UINT32(360000, info.budget_left_ms); /* a boot starts with an empty window */
    /* a boot never reopens more than a fresh start: the lock is the same whatever the clock says */
    subghz_txpolicy_init(&s_p, 0xFFFFFF00u);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, subghz_txpolicy_check(&s_p, 0xFFFFFF00u, &p, NULL));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME,
                      subghz_txpolicy_check(&s_p, 0xFFFFFF00u + 17999u, &p, NULL));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, subghz_txpolicy_check(&s_p, 0xFFFFFF00u + 18000u, &p, NULL));
}

static void test_the_off_time_is_nine_times_each_transmission(void)
{
    static const uint32_t k_dur[] = {1, 2, 52, 100, 1000, 1999, 2000};
    for (size_t i = 0; i < sizeof k_dur / sizeof k_dur[0]; i++) {
        fresh_after_lock();
        uint32_t start = 100000;
        uint32_t d = k_dur[i];
        subghz_txpolicy_record(&s_p, start, start + d, d);
        uint32_t end = start + d;
        uint32_t off = 9u * d;
        subghz_tx_info_t info;
        subghz_tx_plan_t p = plan_us(1000);
        TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, subghz_txpolicy_check(&s_p, end, &p, &info));
        TEST_ASSERT_EQUAL_UINT32(off, info.wait_ms);
        TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME,
                          subghz_txpolicy_check(&s_p, end + off - 1u, &p, &info));
        TEST_ASSERT_EQUAL_UINT32(1, info.wait_ms);
        TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, subghz_txpolicy_check(&s_p, end + off, &p, &info));
        TEST_ASSERT_EQUAL_UINT32(0, info.wait_ms);
    }
}

/* The rule is about the last transmission only, and it follows the next one. */
static void test_the_off_time_follows_the_last_transmission(void)
{
    fresh_after_lock();
    subghz_txpolicy_record(&s_p, 100000, 102000, 2000); /* off until 120000 */
    subghz_txpolicy_record(&s_p, 120000, 120100, 100);  /* off until 121000 */
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, check_ms(120999, 1));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check_ms(121000, 1));
}

static void test_the_budget_to_the_millisecond_and_the_window_edge(void)
{
    fresh_after_lock();
    uint32_t now = SUBGHZ_TX_BOOT_LOCK_MS;
    uint32_t first_end = 0;
    for (int i = 0; i < 180; i++) {
        TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check_ms(now, 2000));
        subghz_txpolicy_record(&s_p, now, now + 2000, 2000);
        if (i == 0) {
            first_end = now + 2000;
        }
        TEST_ASSERT_EQUAL_UINT32(360000u - 2000u * (uint32_t)(i + 1),
                                 subghz_txpolicy_budget_left_ms(&s_p, now + 2000));
        now += 20000; /* 2 s on air and its 18 s off-time: 10 % */
    }
    /* 360 000 ms are charged: not one more millisecond, though the off-time has long passed */
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BUDGET, check_ms(now, 1));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BUDGET, check_ms(first_end + SUBGHZ_TX_WINDOW_MS - 1u, 1));
    /* the first transmission leaves the window at exactly 3600 s after it ended */
    subghz_tx_info_t info;
    subghz_tx_plan_t p = plan_us(2000000u);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK,
                      subghz_txpolicy_check(&s_p, first_end + SUBGHZ_TX_WINDOW_MS, &p, &info));
    TEST_ASSERT_EQUAL_UINT32(2000, info.budget_left_ms);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check_ms(first_end + SUBGHZ_TX_WINDOW_MS, 2000));
}

static void test_the_last_millisecond_of_the_budget(void)
{
    fresh_after_lock();
    /* 359 999 ms in 179 x 2000 + 1999: exactly 1 ms left */
    uint32_t now = SUBGHZ_TX_BOOT_LOCK_MS;
    for (int i = 0; i < 179; i++) {
        subghz_txpolicy_record(&s_p, now, now + 2000, 2000);
        now += 20000;
    }
    subghz_txpolicy_record(&s_p, now, now + 1999, 1999);
    now += 19990;
    TEST_ASSERT_EQUAL_UINT32(1, subghz_txpolicy_budget_left_ms(&s_p, now));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BUDGET, check_ms(now, 2));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check_ms(now, 1));
    subghz_txpolicy_record(&s_p, now, now + 1, 1);
    TEST_ASSERT_EQUAL_UINT32(0, subghz_txpolicy_budget_left_ms(&s_p, now + 1));
}

/* A failed attempt is charged in full, and the later of its real end and its planned end counts. */
static void test_an_attempt_is_charged_in_full_and_ends_when_it_really_ended(void)
{
    fresh_after_lock();
    subghz_txpolicy_record(&s_p, 100000, 100010, 1000); /* it stopped early, we cannot tell how */
    TEST_ASSERT_EQUAL_UINT32(359000, subghz_txpolicy_budget_left_ms(&s_p, 100010));
    /* the planned end (101000) holds, not the 10 ms the call took */
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, check_ms(101000 + 8999, 1));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check_ms(101000 + 9000, 1));

    fresh_after_lock();
    subghz_txpolicy_record(&s_p, 100000, 105000, 1000); /* it ran 5 s: the end is when it ended */
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, check_ms(105000 + 8999, 1));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check_ms(105000 + 9000, 1));
}

static void test_the_check_reports_what_it_found_whatever_the_verdict(void)
{
    fresh_after_lock();
    subghz_txpolicy_record(&s_p, 50000, 52000, 2000);
    subghz_tx_info_t info;
    memset(&info, 0xAA, sizeof info);
    subghz_tx_plan_t p = {100, 10, 51200}; /* out of band */
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BAND, subghz_txpolicy_check(&s_p, 53000, &p, &info));
    TEST_ASSERT_EQUAL_UINT32(52, info.duration_ms);
    TEST_ASSERT_EQUAL_UINT32(358000, info.budget_left_ms);
    TEST_ASSERT_EQUAL_UINT32(17000, info.wait_ms);
    TEST_ASSERT_EQUAL_UINT32(0, info.hash);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BAND, subghz_txpolicy_check(&s_p, 53000, &p, NULL));
}

static void test_a_clock_that_wraps(void)
{
    /* a transmission ends exactly as the 32-bit millisecond clock wraps to 0 */
    subghz_txpolicy_init(&s_p, 0xFFFFFFFFu - 19999u);
    uint32_t t0 = 0xFFFFFFFFu - 19999u + SUBGHZ_TX_BOOT_LOCK_MS;
    subghz_txpolicy_tick(&s_p, t0);
    subghz_txpolicy_record(&s_p, t0, t0 + 2000, 2000);
    uint32_t end = t0 + 2000; /* wrapped past zero */
    TEST_ASSERT_TRUE(end < t0);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, check_ms(end + 17999u, 1));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check_ms(end + 18000u, 1));
    TEST_ASSERT_EQUAL_UINT32(358000, subghz_txpolicy_budget_left_ms(&s_p, end + 3599999u));
    TEST_ASSERT_EQUAL_UINT32(360000, subghz_txpolicy_budget_left_ms(&s_p, end + 3600000u));
}

/* A wait that has passed is forgotten, so the clock cannot make it look current again. */
static void test_a_finished_wait_is_forgotten_before_the_clock_can_wrap(void)
{
    subghz_txpolicy_init(&s_p, 0);
    subghz_txpolicy_tick(&s_p, 17999);
    TEST_ASSERT_TRUE(s_p.have_last);
    subghz_txpolicy_tick(&s_p, 18000);
    TEST_ASSERT_FALSE(s_p.have_last);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check_ms(0x90000000u, 1)); /* more than 2^31 ms later */
    /* and an entry is gone long before it could look young again */
    subghz_txpolicy_record(&s_p, 100000, 101000, 1000);
    subghz_txpolicy_tick(&s_p, 101000 + SUBGHZ_TX_WINDOW_MS);
    TEST_ASSERT_EQUAL_UINT8(0, s_p.n);
    TEST_ASSERT_EQUAL_UINT32(360000, subghz_txpolicy_budget_left_ms(&s_p, 0x90000000u));
}

/* A clock that steps back can only make the policy stricter. */
static void test_a_clock_that_steps_back_is_never_more_permissive(void)
{
    fresh_after_lock();
    subghz_txpolicy_record(&s_p, 100000, 102000, 2000);
    subghz_tx_info_t info;
    subghz_tx_plan_t p = plan_us(1000);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, subghz_txpolicy_check(&s_p, 101000, &p, &info));
    TEST_ASSERT_EQUAL_UINT32(18000, info.wait_ms); /* the whole wait: no time has passed yet */
    TEST_ASSERT_EQUAL_UINT32(358000, info.budget_left_ms);
    subghz_txpolicy_tick(&s_p, 90000); /* far in the past: nothing expires */
    TEST_ASSERT_EQUAL_UINT8(1, s_p.n);
}

static void test_a_full_ledger_merges_and_never_forgets_a_millisecond(void)
{
    fresh_after_lock();
    uint32_t now = SUBGHZ_TX_BOOT_LOCK_MS;
    uint32_t first_end = 0;
    for (unsigned i = 0; i < SUBGHZ_TX_LEDGER + 10u; i++) {
        subghz_txpolicy_record(&s_p, now, now + 1, 1);
        if (i == 0) {
            first_end = now + 1;
        }
        now += 10;
        TEST_ASSERT_TRUE(s_p.n <= SUBGHZ_TX_LEDGER);
        /* never under-counted: at least what is exactly in the window */
        uint32_t exact = i + 1u;
        TEST_ASSERT_EQUAL_UINT32(360000u - exact, subghz_txpolicy_budget_left_ms(&s_p, now));
    }
    TEST_ASSERT_EQUAL_UINT8(SUBGHZ_TX_LEDGER, s_p.n);
    /* the oldest two became one that lives as long as the later: at the first one's own edge it
     * is still charged (stricter than exact), a moment later than the second's it is gone */
    uint32_t used_at_first_edge =
        360000u - subghz_txpolicy_budget_left_ms(&s_p, first_end + SUBGHZ_TX_WINDOW_MS);
    TEST_ASSERT_TRUE(used_at_first_edge >= SUBGHZ_TX_LEDGER + 10u - 1u);
    TEST_ASSERT_EQUAL_UINT32(360000u,
                             subghz_txpolicy_budget_left_ms(&s_p, now + SUBGHZ_TX_WINDOW_MS));
}

/* ---- the transport's own limits (stark_subghz_tx_ok) ---------------------------------------- */

static void test_the_transport_limits(void)
{
    subghz_sym_t s[2049];
    for (size_t i = 0; i < 2049; i++) {
        s[i] = (i & 1u) ? -100 : 100;
    }
    TEST_ASSERT_FALSE(stark_subghz_tx_ok(NULL, 4, 10));
    TEST_ASSERT_FALSE(stark_subghz_tx_ok(s, 0, 10));
    TEST_ASSERT_TRUE(stark_subghz_tx_ok(s, 1, 1 + STARK_SUBGHZ_TX_MARGIN_MS));
    TEST_ASSERT_TRUE(stark_subghz_tx_ok(s, 2048, 100)); /* 204.8 ms of signal */
    TEST_ASSERT_FALSE(stark_subghz_tx_ok(s, 2049, 100));
    /* a symbol of 0, of 1, of 1e6 and of 1e6 + 1, either sign */
    subghz_sym_t one[1] = {0};
    TEST_ASSERT_FALSE(stark_subghz_tx_ok(one, 1, 10));
    one[0] = 1;
    TEST_ASSERT_TRUE(stark_subghz_tx_ok(one, 1, 10));
    one[0] = -1;
    TEST_ASSERT_TRUE(stark_subghz_tx_ok(one, 1, 10));
    one[0] = 1000000;
    TEST_ASSERT_TRUE(stark_subghz_tx_ok(one, 1, 1000 + STARK_SUBGHZ_TX_MARGIN_MS));
    one[0] = -1000000;
    TEST_ASSERT_TRUE(stark_subghz_tx_ok(one, 1, 1000 + STARK_SUBGHZ_TX_MARGIN_MS));
    one[0] = 1000001;
    TEST_ASSERT_FALSE(stark_subghz_tx_ok(one, 1, 2000));
    one[0] = -1000001;
    TEST_ASSERT_FALSE(stark_subghz_tx_ok(one, 1, 2000));
    one[0] = INT32_MIN;
    TEST_ASSERT_FALSE(stark_subghz_tx_ok(one, 1, 2000));
    one[0] = INT32_MAX;
    TEST_ASSERT_FALSE(stark_subghz_tx_ok(one, 1, 2000));
    /* the sum: exactly 2 s is allowed, one microsecond more is not */
    subghz_sym_t two[3] = {1000000, -1000000, 0};
    TEST_ASSERT_TRUE(stark_subghz_tx_ok(two, 2, 2000 + STARK_SUBGHZ_TX_MARGIN_MS));
    two[2] = 1;
    TEST_ASSERT_FALSE(stark_subghz_tx_ok(two, 3, 2000 + STARK_SUBGHZ_TX_MARGIN_MS));
    two[0] = 999999;
    TEST_ASSERT_TRUE(stark_subghz_tx_ok(two, 3, 2000 + STARK_SUBGHZ_TX_MARGIN_MS));
    /* a transmission far over 2 s in a thousand symbols of 1e6 never overflows the sum */
    static subghz_sym_t big[2048];
    for (size_t i = 0; i < 2048; i++) {
        big[i] = 1000000;
    }
    TEST_ASSERT_FALSE(stark_subghz_tx_ok(big, 2048, 2100));
}

static void test_the_transport_timeout_is_bounded_by_the_signal(void)
{
    subghz_sym_t s[2] = {50000, -1500}; /* 51.5 ms: 52 ms rounded up */
    TEST_ASSERT_FALSE(stark_subghz_tx_ok(s, 2, 0));
    TEST_ASSERT_TRUE(stark_subghz_tx_ok(s, 2, 1));
    TEST_ASSERT_TRUE(stark_subghz_tx_ok(s, 2, 52 + STARK_SUBGHZ_TX_MARGIN_MS));
    TEST_ASSERT_FALSE(stark_subghz_tx_ok(s, 2, 52 + STARK_SUBGHZ_TX_MARGIN_MS + 1u));
    TEST_ASSERT_FALSE(stark_subghz_tx_ok(s, 2, UINT32_MAX)); /* never an unbounded wait */
    subghz_sym_t exact[1] = {50000};
    TEST_ASSERT_TRUE(stark_subghz_tx_ok(exact, 1, 50 + STARK_SUBGHZ_TX_MARGIN_MS));
    TEST_ASSERT_FALSE(stark_subghz_tx_ok(exact, 1, 50 + STARK_SUBGHZ_TX_MARGIN_MS + 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_verdict_names);
    RUN_TEST(test_the_limits_are_the_documented_constants);
    RUN_TEST(test_the_span_rounds_up_to_a_millisecond);
    RUN_TEST(test_the_band_edges);
    RUN_TEST(test_the_length_edges);
    RUN_TEST(test_the_duration_edges);
    RUN_TEST(test_the_refusal_order);
    RUN_TEST(test_the_boot_lock_is_the_longest_off_time);
    RUN_TEST(test_the_off_time_is_nine_times_each_transmission);
    RUN_TEST(test_the_off_time_follows_the_last_transmission);
    RUN_TEST(test_the_budget_to_the_millisecond_and_the_window_edge);
    RUN_TEST(test_the_last_millisecond_of_the_budget);
    RUN_TEST(test_an_attempt_is_charged_in_full_and_ends_when_it_really_ended);
    RUN_TEST(test_the_check_reports_what_it_found_whatever_the_verdict);
    RUN_TEST(test_a_clock_that_wraps);
    RUN_TEST(test_a_finished_wait_is_forgotten_before_the_clock_can_wrap);
    RUN_TEST(test_a_clock_that_steps_back_is_never_more_permissive);
    RUN_TEST(test_a_full_ledger_merges_and_never_forgets_a_millisecond);
    RUN_TEST(test_the_transport_limits);
    RUN_TEST(test_the_transport_timeout_is_bounded_by_the_signal);
    return UNITY_END();
}
