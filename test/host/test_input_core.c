/*
 * test_input_core.c — host unit tests for the key state machine (STARK-0011).
 *
 * TESTING.md §2 "Input FSM", driven by an explicit now_ms timeline (one
 * sample per millisecond unless a test says otherwise) — nothing sleeps.
 * Timing injection and the OK+BACK chord: STARK-0106.
 */
#include <string.h>
#include "input_core.h"
#include "unity.h"

#define LOG_MAX 1024
#define BIT(k)  ((uint8_t)(1u << (k)))

typedef struct {
    uint32_t t;
    input_action_t a;
} rec_t;

static input_core_t s_core;
static rec_t s_log[LOG_MAX];
static size_t s_n;
static uint32_t s_t; /* next sample time */

static void sample_at(uint8_t raw, uint32_t now)
{
    input_action_t out[INPUT_CORE_MAX_ACTIONS];
    size_t n = input_core_update(&s_core, raw, now, out, INPUT_CORE_MAX_ACTIONS);
    TEST_ASSERT_LESS_OR_EQUAL(INPUT_CORE_MAX_ACTIONS, n);
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_LESS_THAN(LOG_MAX, s_n);
        s_log[s_n++] = (rec_t){.t = now, .a = out[i]};
    }
}

/* Feeds `raw` once per ms for `ms` ms starting at s_t. */
static void run(uint8_t raw, uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        sample_at(raw, s_t++);
    }
}

static size_t count(stark_key_t key, stark_key_action_t action)
{
    size_t n = 0;
    for (size_t i = 0; i < s_n; i++) {
        n += (s_log[i].a.key == key && s_log[i].a.action == action) ? 1u : 0u;
    }
    return n;
}

/* Time of the nth (0-based) occurrence of key/action; fails if absent. */
static uint32_t when(stark_key_t key, stark_key_action_t action, size_t nth)
{
    for (size_t i = 0; i < s_n; i++) {
        if (s_log[i].a.key == key && s_log[i].a.action == action && nth-- == 0) {
            return s_log[i].t;
        }
    }
    TEST_FAIL_MESSAGE("expected action not emitted");
    return 0;
}

/* Presses `key` from s_t and returns the time PRESS was emitted. */
static uint32_t press(stark_key_t key)
{
    size_t before = count(key, STARK_KEY_PRESS);
    run(BIT(key), INPUT_DEBOUNCE_MS + 1);
    TEST_ASSERT_EQUAL_size_t(before + 1, count(key, STARK_KEY_PRESS));
    return when(key, STARK_KEY_PRESS, before);
}

void setUp(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, input_core_init(&s_core, NULL));
    s_n = 0;
    s_t = 1000;
}

void tearDown(void)
{}

/* ---- debounce (AC #2: 19 ms vs 21 ms) ---------------------------------- */

void test_press_of_19ms_is_ignored_as_bounce(void)
{
    run(BIT(STARK_KEY_OK), 19);
    run(0, 200);
    TEST_ASSERT_EQUAL_size_t(0, s_n);
}

void test_press_of_21ms_registers_20ms_after_the_edge(void)
{
    uint32_t edge = s_t;
    run(BIT(STARK_KEY_OK), 21);
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_OK, STARK_KEY_PRESS));
    TEST_ASSERT_EQUAL_UINT32(edge + INPUT_DEBOUNCE_MS, when(STARK_KEY_OK, STARK_KEY_PRESS, 0));
}

void test_chatter_restarts_the_debounce_window(void)
{
    for (int i = 0; i < 10; i++) { /* 3 ms on / 3 ms off for 60 ms */
        run(BIT(STARK_KEY_UP), 3);
        run(0, 3);
    }
    TEST_ASSERT_EQUAL_size_t(0, s_n);
    uint32_t settle = s_t;
    run(BIT(STARK_KEY_UP), 40);
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_UP, STARK_KEY_PRESS));
    TEST_ASSERT_EQUAL_UINT32(settle + INPUT_DEBOUNCE_MS, when(STARK_KEY_UP, STARK_KEY_PRESS, 0));

    for (int i = 0; i < 10; i++) { /* release chatter */
        run(0, 3);
        run(BIT(STARK_KEY_UP), 3);
    }
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_UP, STARK_KEY_RELEASE));
    run(0, 40);
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_UP, STARK_KEY_RELEASE));
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_UP, STARK_KEY_PRESS));
}

/* ---- short / long (AC #2: 499 ms vs 501 ms) ---------------------------- */

void test_release_at_499ms_is_short_without_long(void)
{
    uint32_t p = press(STARK_KEY_OK);
    /* Held until the release debounce completes exactly 499 ms after PRESS. */
    run(BIT(STARK_KEY_OK), (p + 499 - INPUT_DEBOUNCE_MS) - s_t);
    run(0, 100);
    TEST_ASSERT_EQUAL_UINT32(p + 499, when(STARK_KEY_OK, STARK_KEY_RELEASE, 0));
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_OK, STARK_KEY_SHORT));
    TEST_ASSERT_EQUAL_UINT32(p + 499, when(STARK_KEY_OK, STARK_KEY_SHORT, 0));
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_OK, STARK_KEY_LONG));
}

void test_release_at_501ms_is_long_without_short(void)
{
    uint32_t p = press(STARK_KEY_OK);
    run(BIT(STARK_KEY_OK), (p + 501 - INPUT_DEBOUNCE_MS) - s_t);
    run(0, 100);
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_OK, STARK_KEY_LONG));
    TEST_ASSERT_EQUAL_UINT32(p + INPUT_LONG_PRESS_MS, when(STARK_KEY_OK, STARK_KEY_LONG, 0));
    TEST_ASSERT_EQUAL_UINT32(p + 501, when(STARK_KEY_OK, STARK_KEY_RELEASE, 0));
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_OK, STARK_KEY_SHORT));
}

void test_long_is_emitted_exactly_once_while_held(void)
{
    uint32_t p = press(STARK_KEY_BACK);
    run(BIT(STARK_KEY_BACK), 3000);
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_BACK, STARK_KEY_LONG));
    TEST_ASSERT_EQUAL_UINT32(p + 500, when(STARK_KEY_BACK, STARK_KEY_LONG, 0));
    run(0, 50);
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_BACK, STARK_KEY_RELEASE));
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_BACK, STARK_KEY_SHORT));
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_BACK, STARK_KEY_LONG));
}

void test_second_press_gets_its_own_long_and_short(void)
{
    press(STARK_KEY_OK);
    run(BIT(STARK_KEY_OK), 700);
    run(0, 50);
    press(STARK_KEY_OK);
    run(0, 50);
    TEST_ASSERT_EQUAL_size_t(2, count(STARK_KEY_OK, STARK_KEY_PRESS));
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_OK, STARK_KEY_LONG));
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_OK, STARK_KEY_SHORT)); /* the second one */
    TEST_ASSERT_EQUAL_size_t(2, count(STARK_KEY_OK, STARK_KEY_RELEASE));
}

/* ---- repeat ------------------------------------------------------------- */

void test_repeats_start_at_400ms_and_recur_every_120ms(void)
{
    uint32_t p = press(STARK_KEY_UP);
    run(BIT(STARK_KEY_UP), 1000);
    TEST_ASSERT_EQUAL_size_t(6, count(STARK_KEY_UP, STARK_KEY_REPEAT)); /* 400 .. 1000 */
    for (size_t i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_UINT32(p + INPUT_REPEAT_DELAY_MS + i * INPUT_REPEAT_INTERVAL_MS,
                                 when(STARK_KEY_UP, STARK_KEY_REPEAT, i));
    }
    size_t r = 0;
    for (size_t i = 0; i < s_n; i++) {
        if (s_log[i].a.action == STARK_KEY_REPEAT) {
            TEST_ASSERT_EQUAL_UINT8(++r, s_log[i].a.repeat);
        } else {
            TEST_ASSERT_EQUAL_UINT8(0, s_log[i].a.repeat);
        }
    }
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_UP, STARK_KEY_LONG)); /* directions also LONG */
}

void test_every_direction_key_repeats(void)
{
    static const stark_key_t dirs[] = {STARK_KEY_UP, STARK_KEY_DOWN, STARK_KEY_LEFT,
                                       STARK_KEY_RIGHT};
    for (size_t d = 0; d < 4; d++) {
        uint32_t p = press(dirs[d]);
        run(BIT(dirs[d]), 400);
        run(0, 50);
        TEST_ASSERT_EQUAL_size_t(1, count(dirs[d], STARK_KEY_REPEAT));
        TEST_ASSERT_EQUAL_UINT32(p + 400, when(dirs[d], STARK_KEY_REPEAT, 0));
    }
}

void test_ok_and_back_never_repeat(void)
{
    press(STARK_KEY_OK);
    run(BIT(STARK_KEY_OK), 2000);
    press(STARK_KEY_BACK);
    run(BIT(STARK_KEY_BACK), 2000);
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_OK, STARK_KEY_REPEAT));
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_BACK, STARK_KEY_REPEAT));
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_OK, STARK_KEY_LONG));
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_BACK, STARK_KEY_LONG));
}

void test_repeat_counter_saturates_at_255(void)
{
    press(STARK_KEY_DOWN);
    for (int i = 0; i < 300 * 24; i++) { /* 5 ms samples, ~36 s: 300 repeats */
        sample_at(BIT(STARK_KEY_DOWN), s_t);
        s_t += 5;
    }
    TEST_ASSERT_GREATER_THAN_size_t(255, count(STARK_KEY_DOWN, STARK_KEY_REPEAT));
    TEST_ASSERT_EQUAL_UINT8(255, s_log[s_n - 1].a.repeat);
    TEST_ASSERT_EQUAL(STARK_KEY_REPEAT, s_log[s_n - 1].a.action);
}

/* ---- independence and the bitmap --------------------------------------- */

void test_two_keys_are_tracked_independently(void)
{
    uint32_t up = press(STARK_KEY_UP);
    run(BIT(STARK_KEY_UP), 100);
    run(BIT(STARK_KEY_UP) | BIT(STARK_KEY_DOWN), INPUT_DEBOUNCE_MS + 1);
    uint32_t down = when(STARK_KEY_DOWN, STARK_KEY_PRESS, 0);
    TEST_ASSERT_EQUAL_UINT32(up + 121, down);

    run(BIT(STARK_KEY_UP) | BIT(STARK_KEY_DOWN), 500); /* UP passes its LONG */
    run(BIT(STARK_KEY_DOWN), 700);                     /* UP released, DOWN still held */

    TEST_ASSERT_EQUAL_UINT32(up + 400, when(STARK_KEY_UP, STARK_KEY_REPEAT, 0));
    TEST_ASSERT_EQUAL_UINT32(down + 400, when(STARK_KEY_DOWN, STARK_KEY_REPEAT, 0));
    TEST_ASSERT_EQUAL_UINT32(up + 500, when(STARK_KEY_UP, STARK_KEY_LONG, 0));
    TEST_ASSERT_EQUAL_UINT32(down + 500, when(STARK_KEY_DOWN, STARK_KEY_LONG, 0));
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_UP, STARK_KEY_RELEASE));
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_DOWN, STARK_KEY_RELEASE));
    /* DOWN kept repeating on its own grid after UP was released */
    TEST_ASSERT_EQUAL_UINT32(down + 400 + 5 * 120, when(STARK_KEY_DOWN, STARK_KEY_REPEAT, 5));
}

void test_each_bit_maps_to_its_key_and_high_bits_are_ignored(void)
{
    run(0xC0, 200); /* bits 6 and 7: no such keys */
    TEST_ASSERT_EQUAL_size_t(0, s_n);
    for (int k = 0; k < STARK_KEY_COUNT; k++) {
        s_n = 0;
        run(BIT(k), 30);
        run(0, 30);
        TEST_ASSERT_EQUAL_size_t(3, s_n); /* PRESS, RELEASE, SHORT */
        for (size_t i = 0; i < s_n; i++) {
            TEST_ASSERT_EQUAL(k, s_log[i].a.key);
        }
    }
}

void test_all_six_keys_at_once_fit_in_max_actions(void)
{
    const uint8_t all = (uint8_t)((1u << STARK_KEY_COUNT) - 1u);
    run(all, 30);
    /* six PRESS on one sample, then the OK+BACK chord */
    TEST_ASSERT_EQUAL_size_t(STARK_KEY_COUNT + 1, s_n);
    TEST_ASSERT_EQUAL(STARK_KEY_CHORD, s_log[STARK_KEY_COUNT].a.action);
    s_n = 0;
    run(0, INPUT_DEBOUNCE_MS); /* nothing yet */
    TEST_ASSERT_EQUAL_size_t(0, s_n);
    sample_at(0, s_t++); /* every key's release debounces on the same sample */
    /* RELEASE + SHORT for the four directions, RELEASE alone for the chord's keys */
    TEST_ASSERT_EQUAL_size_t(4 * 2 + 2, s_n);
    TEST_ASSERT_LESS_OR_EQUAL(INPUT_CORE_MAX_ACTIONS, s_n);
    for (size_t i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL(i / 2, s_log[i].a.key); /* key order */
        TEST_ASSERT_EQUAL(i % 2 ? STARK_KEY_SHORT : STARK_KEY_RELEASE, s_log[i].a.action);
    }
    TEST_ASSERT_EQUAL(STARK_KEY_OK, s_log[8].a.key);
    TEST_ASSERT_EQUAL(STARK_KEY_RELEASE, s_log[8].a.action);
    TEST_ASSERT_EQUAL(STARK_KEY_BACK, s_log[9].a.key);
    TEST_ASSERT_EQUAL(STARK_KEY_RELEASE, s_log[9].a.action);
}

/* ---- timing injection (STARK-0106) --------------------------------------- */

void test_custom_timing_is_honoured(void)
{
    const input_core_timing_t t = {10, 300, 200, 50};
    TEST_ASSERT_EQUAL(STARK_OK, input_core_init(&s_core, &t));
    uint32_t edge = s_t;
    run(BIT(STARK_KEY_UP), 11);
    uint32_t p = when(STARK_KEY_UP, STARK_KEY_PRESS, 0);
    TEST_ASSERT_EQUAL_UINT32(edge + 10, p);
    run(BIT(STARK_KEY_UP), 400);
    TEST_ASSERT_EQUAL_UINT32(p + 200, when(STARK_KEY_UP, STARK_KEY_REPEAT, 0));
    TEST_ASSERT_EQUAL_UINT32(p + 250, when(STARK_KEY_UP, STARK_KEY_REPEAT, 1));
    TEST_ASSERT_EQUAL_UINT32(p + 300, when(STARK_KEY_UP, STARK_KEY_LONG, 0));
}

void test_invalid_timing_is_rejected_and_core_untouched(void)
{
    press(STARK_KEY_OK); /* some state to preserve */
    input_core_t before;
    memcpy(&before, &s_core, sizeof before);
    static const input_core_timing_t bad[] = {
        {20, 500, 400, INPUT_REPEAT_MIN_MS - 1}, /* repeat interval too short */
        {20, 500, 19, 120},                      /* repeat delay below debounce */
        {20, 20, 400, 120},                      /* long press not above debounce */
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, input_core_init(&s_core, &bad[i]));
        TEST_ASSERT_EQUAL_MEMORY(&before, &s_core, sizeof before);
    }
    const input_core_timing_t edge = {20, 21, 20, INPUT_REPEAT_MIN_MS}; /* all at the limit */
    TEST_ASSERT_EQUAL(STARK_OK, input_core_init(&s_core, &edge));
}

/* ---- the OK+BACK chord (STARK-0106) --------------------------------------- */

void test_chord_ok_then_back(void)
{
    press(STARK_KEY_OK);
    run(BIT(STARK_KEY_OK) | BIT(STARK_KEY_BACK), INPUT_DEBOUNCE_MS + 1);
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_OK, STARK_KEY_CHORD));
    TEST_ASSERT_EQUAL_UINT32(when(STARK_KEY_BACK, STARK_KEY_PRESS, 0),
                             when(STARK_KEY_OK, STARK_KEY_CHORD, 0));
    run(BIT(STARK_KEY_OK) | BIT(STARK_KEY_BACK), 100);
    run(0, 60);
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_OK, STARK_KEY_RELEASE));
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_BACK, STARK_KEY_RELEASE));
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_OK, STARK_KEY_SHORT));
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_BACK, STARK_KEY_SHORT));
}

void test_chord_back_then_ok_and_both_at_once(void)
{
    press(STARK_KEY_BACK);
    run(BIT(STARK_KEY_OK) | BIT(STARK_KEY_BACK), INPUT_DEBOUNCE_MS + 1);
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_OK, STARK_KEY_CHORD));
    run(0, 60);
    run(BIT(STARK_KEY_OK) | BIT(STARK_KEY_BACK), INPUT_DEBOUNCE_MS + 1); /* both on one sample */
    TEST_ASSERT_EQUAL_size_t(2, count(STARK_KEY_OK, STARK_KEY_CHORD));
    TEST_ASSERT_EQUAL(STARK_KEY_CHORD, s_log[s_n - 1].a.action); /* after both PRESS */
}

void test_chord_suppresses_long_while_held(void)
{
    run(BIT(STARK_KEY_OK) | BIT(STARK_KEY_BACK), 900);
    run(0, 60);
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_OK, STARK_KEY_CHORD));
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_OK, STARK_KEY_LONG));
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_BACK, STARK_KEY_LONG));
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_OK, STARK_KEY_SHORT));
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_BACK, STARK_KEY_SHORT));
}

void test_chord_stays_latched_until_both_released(void)
{
    run(BIT(STARK_KEY_OK) | BIT(STARK_KEY_BACK), 50);
    run(BIT(STARK_KEY_BACK), 50);                     /* OK released, BACK held */
    run(BIT(STARK_KEY_OK) | BIT(STARK_KEY_BACK), 50); /* OK pressed again */
    TEST_ASSERT_EQUAL_size_t(2, count(STARK_KEY_OK, STARK_KEY_PRESS));
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_OK, STARK_KEY_CHORD)); /* no second chord */
    run(0, 60);
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_OK, STARK_KEY_SHORT));
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_BACK, STARK_KEY_SHORT));
    /* both released: the chord is free again, and plain OK works normally */
    run(BIT(STARK_KEY_OK), 50);
    run(0, 60);
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_OK, STARK_KEY_SHORT));
    run(BIT(STARK_KEY_OK) | BIT(STARK_KEY_BACK), 50);
    TEST_ASSERT_EQUAL_size_t(2, count(STARK_KEY_OK, STARK_KEY_CHORD));
}

void test_chord_after_ok_long_suppresses_back_long(void)
{
    uint32_t p = press(STARK_KEY_OK);
    run(BIT(STARK_KEY_OK), 600);
    TEST_ASSERT_EQUAL_UINT32(p + 500, when(STARK_KEY_OK, STARK_KEY_LONG, 0));
    run(BIT(STARK_KEY_OK) | BIT(STARK_KEY_BACK), 800);
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_OK, STARK_KEY_CHORD));
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_BACK, STARK_KEY_LONG));
    run(0, 60);
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_BACK, STARK_KEY_SHORT));
}

/* ---- update-rate independence ------------------------------------------ */

void test_a_gap_between_updates_does_not_burst_repeats(void)
{
    uint32_t p = press(STARK_KEY_LEFT);
    s_t = p + 1000; /* next sample a whole second later */
    run(BIT(STARK_KEY_LEFT), 1);
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_LEFT, STARK_KEY_REPEAT));
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_LEFT, STARK_KEY_LONG));
    /* the repeat (due at 400) is reported before the long (due at 500) */
    TEST_ASSERT_EQUAL(STARK_KEY_REPEAT, s_log[s_n - 2].a.action);
    TEST_ASSERT_EQUAL(STARK_KEY_LONG, s_log[s_n - 1].a.action);
    run(BIT(STARK_KEY_LEFT), 200);
    TEST_ASSERT_EQUAL_UINT32(p + 1120, when(STARK_KEY_LEFT, STARK_KEY_REPEAT, 1));
}

void test_release_seen_late_emits_long_then_release_never_short(void)
{
    uint32_t p = press(STARK_KEY_OK);
    s_t = p + 490;
    sample_at(0, s_t); /* raw release sampled at 490: not yet debounced */
    s_t = p + 800;
    sample_at(0, s_t); /* next sample much later: release confirmed */
    TEST_ASSERT_EQUAL(STARK_KEY_LONG, s_log[s_n - 2].a.action);
    TEST_ASSERT_EQUAL(STARK_KEY_RELEASE, s_log[s_n - 1].a.action);
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_OK, STARK_KEY_SHORT));
}

void test_now_ms_may_wrap(void)
{
    s_t = UINT32_MAX - 100;
    uint32_t p = press(STARK_KEY_RIGHT);
    run(BIT(STARK_KEY_RIGHT), 600);
    TEST_ASSERT_EQUAL_UINT32(p + 400, when(STARK_KEY_RIGHT, STARK_KEY_REPEAT, 0));
    TEST_ASSERT_EQUAL_UINT32(p + 500, when(STARK_KEY_RIGHT, STARK_KEY_LONG, 0));
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_RIGHT, STARK_KEY_LONG));
}

/* ---- API edges ---------------------------------------------------------- */

void test_small_out_discards_excess_but_state_advances(void)
{
    const uint8_t all = (uint8_t)((1u << STARK_KEY_COUNT) - 1u);
    input_action_t out[3];
    size_t got = 0;
    for (uint32_t t = 0; t < 30; t++) {
        got += input_core_update(&s_core, all, s_t++, out, 3);
    }
    TEST_ASSERT_EQUAL_size_t(3, got); /* 6 PRESSes due, only 3 fit */
    TEST_ASSERT_EQUAL(STARK_KEY_UP, out[0].key);
    TEST_ASSERT_EQUAL(STARK_KEY_LEFT, out[2].key);
    /* no PRESS is re-emitted: the discarded ones were still applied */
    run(all, 10);
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_BACK, STARK_KEY_PRESS));
    run(0, 30);
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_BACK, STARK_KEY_RELEASE));
}

void test_null_out_advances_state_and_null_core_is_ignored(void)
{
    for (uint32_t t = 0; t < 30; t++) {
        TEST_ASSERT_EQUAL_size_t(0, input_core_update(&s_core, BIT(STARK_KEY_OK), s_t++, NULL, 8));
    }
    run(0, 30);
    TEST_ASSERT_EQUAL_size_t(0, count(STARK_KEY_OK, STARK_KEY_PRESS));
    TEST_ASSERT_EQUAL_size_t(1, count(STARK_KEY_OK, STARK_KEY_RELEASE));

    input_action_t out[INPUT_CORE_MAX_ACTIONS];
    TEST_ASSERT_EQUAL_size_t(0, input_core_update(NULL, 0x3F, 0, out, INPUT_CORE_MAX_ACTIONS));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, input_core_init(NULL, NULL));
}

void test_init_forgets_a_held_key(void)
{
    press(STARK_KEY_OK);
    TEST_ASSERT_EQUAL(STARK_OK, input_core_init(&s_core, NULL));
    s_n = 0;
    run(0, 100);
    TEST_ASSERT_EQUAL_size_t(0, s_n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_press_of_19ms_is_ignored_as_bounce);
    RUN_TEST(test_press_of_21ms_registers_20ms_after_the_edge);
    RUN_TEST(test_chatter_restarts_the_debounce_window);
    RUN_TEST(test_release_at_499ms_is_short_without_long);
    RUN_TEST(test_release_at_501ms_is_long_without_short);
    RUN_TEST(test_long_is_emitted_exactly_once_while_held);
    RUN_TEST(test_second_press_gets_its_own_long_and_short);
    RUN_TEST(test_repeats_start_at_400ms_and_recur_every_120ms);
    RUN_TEST(test_every_direction_key_repeats);
    RUN_TEST(test_ok_and_back_never_repeat);
    RUN_TEST(test_repeat_counter_saturates_at_255);
    RUN_TEST(test_two_keys_are_tracked_independently);
    RUN_TEST(test_each_bit_maps_to_its_key_and_high_bits_are_ignored);
    RUN_TEST(test_all_six_keys_at_once_fit_in_max_actions);
    RUN_TEST(test_custom_timing_is_honoured);
    RUN_TEST(test_invalid_timing_is_rejected_and_core_untouched);
    RUN_TEST(test_chord_ok_then_back);
    RUN_TEST(test_chord_back_then_ok_and_both_at_once);
    RUN_TEST(test_chord_suppresses_long_while_held);
    RUN_TEST(test_chord_stays_latched_until_both_released);
    RUN_TEST(test_chord_after_ok_long_suppresses_back_long);
    RUN_TEST(test_a_gap_between_updates_does_not_burst_repeats);
    RUN_TEST(test_release_seen_late_emits_long_then_release_never_short);
    RUN_TEST(test_now_ms_may_wrap);
    RUN_TEST(test_small_out_discards_excess_but_state_advances);
    RUN_TEST(test_null_out_advances_state_and_null_core_is_ignored);
    RUN_TEST(test_init_forgets_a_held_key);
    return UNITY_END();
}
