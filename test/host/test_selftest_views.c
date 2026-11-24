/*
 * test_selftest_views.c — the Self-Test app's rows and operator walk (TASKS.md STARK-0707): the
 * home rows and their text, the Buttons step (every key, any order, a repeat counts once, the 30 s
 * limit naming the missing keys), the attested steps (OK is a yes and never a pass, BACK is a no
 * and ends the walk, an answer needs the press to have arrived in the step), the replay, and the
 * texts. Pure; no UI.
 */
#include <stdio.h>
#include <string.h>
#include "selftestapp.h"
#include "stark_input.h"
#include "unity.h"

static stwalk_t w;
static stwalk_out_t out;

void setUp(void)
{
    stwalk_start(&w, 1000);
    memset(&out, 0, sizeof out);
}
void tearDown(void)
{}

static void press(uint8_t key)
{
    stwalk_key(&w, key, STARK_KEY_PRESS, &out);
}

/* A whole short press: what the input service emits for a key that is pressed and released. */
static void tap(uint8_t key)
{
    press(key);
    stwalk_key(&w, key, STARK_KEY_RELEASE, &out);
    stwalk_key(&w, key, STARK_KEY_SHORT, &out);
}

static void all_keys(void)
{
    for (uint8_t k = 0; k < 6; k++) {
        press(k);
    }
}

/* ---- the home rows ---------------------------------------------------------------------- */

static void test_rows_are_run_summary_the_checks_and_export(void)
{
    strow_t rows[STAPP_ROWS_MAX];
    TEST_ASSERT_EQUAL_size_t(13, stview_rows(10, rows));
    TEST_ASSERT_EQUAL(STROW_RUN, rows[0].kind);
    TEST_ASSERT_EQUAL(STROW_SUMMARY, rows[1].kind);
    for (size_t i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL(STROW_CHECK, rows[2 + i].kind);
        TEST_ASSERT_EQUAL_UINT8(i, rows[2 + i].idx);
    }
    TEST_ASSERT_EQUAL(STROW_EXPORT, rows[12].kind);
    TEST_ASSERT_EQUAL_size_t(3, stview_rows(0, rows));
    TEST_ASSERT_EQUAL(STROW_EXPORT, rows[2].kind);
    /* a table longer than the screen state: clamped, the export row always last */
    TEST_ASSERT_EQUAL_size_t(STAPP_ROWS_MAX, stview_rows(40, rows));
    TEST_ASSERT_EQUAL(STROW_EXPORT, rows[STAPP_ROWS_MAX - 1u].kind);
}

static void test_labels_and_values(void)
{
    stark_selftest_row_t res[5] = {{"RAM", false, ST_PASS, "", 0},
                                   {"Flash", false, ST_FAIL, "", 0},
                                   {"Storage", false, ST_SKIP, "", 0},
                                   {"Display", true, ST_ATTESTED, "", 0},
                                   {"Buttons", false, ST_NOT_RUN, "", 0}};
    strow_t run = {STROW_RUN, 0}, sum = {STROW_SUMMARY, 0}, exp = {STROW_EXPORT, 0};
    TEST_ASSERT_EQUAL_STRING("Run all", stview_label(&run, res, 5, "x"));
    TEST_ASSERT_EQUAL_STRING("7 pass", stview_label(&sum, res, 5, "7 pass"));
    TEST_ASSERT_EQUAL_STRING("", stview_label(&sum, res, 5, NULL));
    TEST_ASSERT_EQUAL_STRING("Export report", stview_label(&exp, res, 5, "x"));
    static const char *const k_value[5] = {"pass", "FAIL", "skip", "attested", "-"};
    for (uint8_t i = 0; i < 5; i++) {
        strow_t c = {STROW_CHECK, i};
        TEST_ASSERT_EQUAL_STRING(res[i].name, stview_label(&c, res, 5, "x"));
        TEST_ASSERT_EQUAL_STRING(k_value[i], stview_value(&c, res, 5));
    }
    strow_t past = {STROW_CHECK, 5};
    TEST_ASSERT_EQUAL_STRING("?", stview_label(&past, res, 5, "x"));
    TEST_ASSERT_NULL(stview_value(&past, res, 5));
    TEST_ASSERT_NULL(stview_value(&run, res, 5));
    TEST_ASSERT_NULL(stview_value(&sum, res, 5));
    TEST_ASSERT_NULL(stview_value(&exp, res, 5));
    res[0].result = (stark_selftest_result_t)9; /* an unknown value reads as not run */
    strow_t c0 = {STROW_CHECK, 0};
    TEST_ASSERT_EQUAL_STRING("-", stview_value(&c0, res, 5));
}

static void test_the_screen_summary_is_compact_while_some_checks_are_not_run(void)
{
    stark_selftest_row_t res[10];
    memset(res, 0, sizeof res);
    char sum[64];
    stview_summary(res, 10, sum, sizeof sum);
    TEST_ASSERT_EQUAL_STRING("0 pass  0 att  0 fail  0 skip  10 left", sum);
    stview_summary(res, 0, sum, sizeof sum);
    TEST_ASSERT_EQUAL_STRING("0 pass  0 attested  0 fail  0 skip", sum);
    for (size_t i = 0; i < 10; i++) {
        res[i].result = i < 7 ? ST_PASS : ST_ATTESTED;
    }
    stview_summary(res, 10, sum, sizeof sum);
    TEST_ASSERT_EQUAL_STRING("7 pass  3 attested  0 fail  0 skip", sum);
    res[0].result = ST_FAIL;
    res[1].result = ST_SKIP;
    res[9].result = ST_NOT_RUN;
    stview_summary(res, 10, sum, sizeof sum);
    TEST_ASSERT_EQUAL_STRING("5 pass  2 att  1 fail  1 skip  1 left", sum);
    TEST_ASSERT_TRUE(strlen(sum) <= 38u);
    res[2].result = (stark_selftest_result_t)9; /* an unknown value is counted as nothing */
    stview_summary(res, 10, sum, sizeof sum);
    TEST_ASSERT_EQUAL_STRING("4 pass  2 att  1 fail  1 skip  1 left", sum);
    memset(res, 0, sizeof res);
    stview_summary(res, 10, sum, 8); /* cut to the buffer, terminated */
    TEST_ASSERT_EQUAL_size_t(7, strlen(sum));
}

static void test_results_in_words(void)
{
    TEST_ASSERT_EQUAL_STRING("not run", stview_result_word(ST_NOT_RUN));
    TEST_ASSERT_EQUAL_STRING("pass", stview_result_word(ST_PASS));
    TEST_ASSERT_EQUAL_STRING("fail", stview_result_word(ST_FAIL));
    TEST_ASSERT_EQUAL_STRING("skip", stview_result_word(ST_SKIP));
    TEST_ASSERT_EQUAL_STRING("attested", stview_result_word(ST_ATTESTED));
    TEST_ASSERT_EQUAL_STRING("not run", stview_result_word((stark_selftest_result_t)9));
}

/* ---- the Buttons step ------------------------------------------------------------------- */

static void test_buttons_pass_when_every_key_was_pressed_in_any_order(void)
{
    TEST_ASSERT_EQUAL(STWALK_BUTTONS, w.step);
    const uint8_t order[] = {5, 4, 0, 3, 1, 2};
    for (size_t i = 0; i < 5; i++) {
        press(order[i]);
        TEST_ASSERT_TRUE(out.consumed);
        TEST_ASSERT_FALSE(out.have);
        TEST_ASSERT_EQUAL(STWALK_BUTTONS, w.step);
    }
    press(order[5]);
    TEST_ASSERT_TRUE(out.have);
    TEST_ASSERT_EQUAL_size_t(STAPP_BUTTONS, out.index);
    TEST_ASSERT_EQUAL(ST_PASS, out.result);
    TEST_ASSERT_EQUAL_STRING("all 6 keys seen", out.evidence);
    TEST_ASSERT_TRUE(out.entered);
    TEST_ASSERT_FALSE(out.finished);
    TEST_ASSERT_EQUAL(STWALK_DISPLAY, w.step);
}

static void test_a_key_pressed_twice_counts_once_and_other_actions_count_for_nothing(void)
{
    for (int i = 0; i < 5; i++) {
        press(0);
    }
    stwalk_key(&w, 1, STARK_KEY_RELEASE, &out);
    stwalk_key(&w, 1, STARK_KEY_REPEAT, &out);
    stwalk_key(&w, 1, STARK_KEY_SHORT, &out);
    stwalk_key(&w, 1, STARK_KEY_LONG, &out);
    stwalk_key(&w, 1, STARK_KEY_CHORD, &out);
    TEST_ASSERT_TRUE(out.consumed);
    TEST_ASSERT_FALSE(out.have);
    TEST_ASSERT_EQUAL_UINT8(1, w.seen);
    stwalk_key(&w, 6, STARK_KEY_PRESS, &out); /* a key the board does not have */
    TEST_ASSERT_TRUE(out.consumed);
    TEST_ASSERT_EQUAL_UINT8(1, w.seen);
    stwalk_key(&w, 255, STARK_KEY_PRESS, &out);
    TEST_ASSERT_EQUAL_UINT8(1, w.seen);
}

static void test_ok_and_back_are_keys_to_press_in_this_step_not_answers(void)
{
    for (uint8_t k = 0; k < 4; k++) {
        press(k);
    }
    tap(STARK_KEY_OK);
    tap(STARK_KEY_BACK); /* BACK completes the six: it is observed, it does not end anything */
    TEST_ASSERT_EQUAL(STWALK_DISPLAY, w.step);
    TEST_ASSERT_FALSE(out.finished);
    stwalk_key(&w, STARK_KEY_BACK, STARK_KEY_RELEASE, &out);
    stwalk_key(&w, STARK_KEY_BACK, STARK_KEY_SHORT, &out);
    TEST_ASSERT_FALSE_MESSAGE(out.have, "the release of the last key answered the next question");
    TEST_ASSERT_EQUAL(STWALK_DISPLAY, w.step);
    TEST_ASSERT_TRUE(out.consumed);
}

static void test_buttons_time_out_after_30_s_and_name_the_missing_keys(void)
{
    press(0);
    press(4);
    stwalk_tick(&w, 1000 + STAPP_BUTTONS_TIMEOUT_MS - 1u, &out);
    TEST_ASSERT_FALSE(out.have);
    TEST_ASSERT_EQUAL(STWALK_BUTTONS, w.step);
    stwalk_tick(&w, 1000 + STAPP_BUTTONS_TIMEOUT_MS, &out);
    TEST_ASSERT_TRUE(out.have);
    TEST_ASSERT_EQUAL_size_t(STAPP_BUTTONS, out.index);
    TEST_ASSERT_EQUAL(ST_FAIL, out.result);
    TEST_ASSERT_EQUAL_STRING("missing DOWN,LEFT,RIGHT,BACK", out.evidence);
    TEST_ASSERT_TRUE(out.entered);
    TEST_ASSERT_EQUAL(STWALK_DISPLAY, w.step); /* the walk goes on to the questions */
}

static void test_a_timeout_names_every_key_or_just_one(void)
{
    stwalk_tick(&w, 1000 + STAPP_BUTTONS_TIMEOUT_MS + 5u, &out);
    TEST_ASSERT_EQUAL_STRING("missing UP,DOWN,LEFT,RIGHT,OK,BACK", out.evidence);
    TEST_ASSERT_TRUE(strlen(out.evidence) <= STARK_SELFTEST_EVIDENCE);
    stwalk_start(&w, 5);
    for (uint8_t k = 0; k < 5; k++) {
        press(k);
    }
    stwalk_tick(&w, 5u + STAPP_BUTTONS_TIMEOUT_MS, &out);
    TEST_ASSERT_EQUAL_STRING("missing BACK", out.evidence);
}

static void test_the_timeout_survives_the_clock_wrapping(void)
{
    stwalk_start(&w, 0xFFFFFF00u);
    stwalk_tick(&w, 0x00000010u, &out); /* 272 ms later */
    TEST_ASSERT_FALSE(out.have);
    stwalk_tick(&w, 0xFFFFFF00u + STAPP_BUTTONS_TIMEOUT_MS, &out);
    TEST_ASSERT_TRUE(out.have);
}

static void test_a_tick_outside_the_buttons_step_does_nothing(void)
{
    all_keys();
    TEST_ASSERT_EQUAL(STWALK_DISPLAY, w.step);
    stwalk_tick(&w, 1000 + 10u * STAPP_BUTTONS_TIMEOUT_MS, &out);
    TEST_ASSERT_FALSE(out.have);
    TEST_ASSERT_FALSE(out.consumed);
    TEST_ASSERT_EQUAL(STWALK_DISPLAY, w.step);
}

/* ---- the attested steps ----------------------------------------------------------------- */

static void test_a_yes_is_attested_never_pass_and_walks_on(void)
{
    all_keys();
    tap(STARK_KEY_OK);
    TEST_ASSERT_TRUE(out.have);
    TEST_ASSERT_EQUAL_size_t(STAPP_DISPLAY, out.index);
    TEST_ASSERT_EQUAL(ST_ATTESTED, out.result);
    TEST_ASSERT_EQUAL_STRING("operator: bars and ramp look right", out.evidence);
    TEST_ASSERT_TRUE(out.entered);
    TEST_ASSERT_EQUAL(STWALK_BUZZER, w.step);
    tap(STARK_KEY_OK);
    TEST_ASSERT_EQUAL_size_t(STAPP_BUZZER, out.index);
    TEST_ASSERT_EQUAL(ST_ATTESTED, out.result);
    TEST_ASSERT_EQUAL_STRING("operator: heard three tones", out.evidence);
    TEST_ASSERT_EQUAL(STWALK_LED, w.step);
    tap(STARK_KEY_OK);
    TEST_ASSERT_EQUAL_size_t(STAPP_LED, out.index);
    TEST_ASSERT_EQUAL(ST_ATTESTED, out.result);
    TEST_ASSERT_EQUAL_STRING("operator: blinks once a second", out.evidence);
    TEST_ASSERT_TRUE(out.finished);
    TEST_ASSERT_FALSE(out.entered);
    TEST_ASSERT_EQUAL(STWALK_NONE, w.step);
}

static void test_back_is_a_no_that_records_a_failure_and_ends_the_walk(void)
{
    struct {
        int oks;
        size_t index;
        const char *text;
    } k[] = {{0, STAPP_DISPLAY, "operator: not right"},
             {1, STAPP_BUZZER, "operator: heard nothing"},
             {2, STAPP_LED, "operator: no blink"}};
    for (size_t i = 0; i < 3; i++) {
        stwalk_start(&w, 0);
        all_keys();
        for (int o = 0; o < k[i].oks; o++) {
            tap(STARK_KEY_OK);
        }
        tap(STARK_KEY_BACK);
        TEST_ASSERT_TRUE(out.have);
        TEST_ASSERT_EQUAL_size_t(k[i].index, out.index);
        TEST_ASSERT_EQUAL(ST_FAIL, out.result);
        TEST_ASSERT_EQUAL_STRING(k[i].text, out.evidence);
        TEST_ASSERT_TRUE(out.finished);
        TEST_ASSERT_EQUAL(STWALK_NONE, w.step);
        /* nothing after the end belongs to the walk */
        stwalk_key(&w, STARK_KEY_OK, STARK_KEY_SHORT, &out);
        TEST_ASSERT_FALSE(out.consumed);
        TEST_ASSERT_FALSE(out.have);
    }
}

static void test_an_answer_needs_its_press_to_have_arrived_in_the_step(void)
{
    all_keys();
    stwalk_key(&w, STARK_KEY_OK, STARK_KEY_SHORT, &out); /* the tail of an earlier press */
    TEST_ASSERT_FALSE(out.have);
    TEST_ASSERT_TRUE(out.consumed);
    stwalk_key(&w, STARK_KEY_BACK, STARK_KEY_SHORT, &out);
    TEST_ASSERT_FALSE(out.have);
    TEST_ASSERT_EQUAL(STWALK_DISPLAY, w.step);
    press(STARK_KEY_OK);
    TEST_ASSERT_FALSE(out.have); /* a press alone is not an answer */
    stwalk_key(&w, STARK_KEY_OK, STARK_KEY_RELEASE, &out);
    TEST_ASSERT_FALSE(out.have);
    stwalk_key(&w, STARK_KEY_OK, STARK_KEY_SHORT, &out);
    TEST_ASSERT_TRUE(out.have);
}

static void test_the_other_keys_answer_nothing_and_up_replays_the_tones(void)
{
    all_keys();
    tap(STARK_KEY_OK); /* display */
    TEST_ASSERT_EQUAL(STWALK_BUZZER, w.step);
    tap(STARK_KEY_LEFT);
    tap(STARK_KEY_RIGHT);
    tap(STARK_KEY_DOWN);
    TEST_ASSERT_FALSE(out.have);
    TEST_ASSERT_FALSE(out.replay);
    press(STARK_KEY_UP);
    TEST_ASSERT_TRUE(out.replay);
    TEST_ASSERT_FALSE(out.have);
    stwalk_key(&w, STARK_KEY_UP, STARK_KEY_SHORT, &out);
    TEST_ASSERT_FALSE(out.replay);
    TEST_ASSERT_EQUAL(STWALK_BUZZER, w.step);
    /* UP is nothing in the other steps */
    tap(STARK_KEY_OK);
    TEST_ASSERT_EQUAL(STWALK_LED, w.step);
    press(STARK_KEY_UP);
    TEST_ASSERT_FALSE(out.replay);
}

static void test_the_walk_is_not_consumed_before_it_starts(void)
{
    stwalk_t idle = {0};
    stwalk_key(&idle, STARK_KEY_OK, STARK_KEY_PRESS, &out);
    TEST_ASSERT_FALSE(out.consumed);
    TEST_ASSERT_FALSE(out.have);
    stwalk_tick(&idle, 99999, &out);
    TEST_ASSERT_FALSE(out.consumed);
}

/* ---- text ------------------------------------------------------------------------------- */

static void test_step_texts(void)
{
    TEST_ASSERT_EQUAL_STRING("Buttons", stview_step_title(STWALK_BUTTONS));
    TEST_ASSERT_EQUAL_STRING("Display", stview_step_title(STWALK_DISPLAY));
    TEST_ASSERT_EQUAL_STRING("Buzzer", stview_step_title(STWALK_BUZZER));
    TEST_ASSERT_EQUAL_STRING("Status LED", stview_step_title(STWALK_LED));
    TEST_ASSERT_EQUAL_STRING("Self-Test", stview_step_title(STWALK_NONE));
    TEST_ASSERT_EQUAL_STRING("Press each key once", stview_step_text(STWALK_BUTTONS));
    TEST_ASSERT_EQUAL_STRING("Colour bars and grey ramp: right?", stview_step_text(STWALK_DISPLAY));
    TEST_ASSERT_EQUAL_STRING("Three rising tones: heard?", stview_step_text(STWALK_BUZZER));
    TEST_ASSERT_EQUAL_STRING("Does the LED blink once a second?", stview_step_text(STWALK_LED));
    TEST_ASSERT_EQUAL_STRING("", stview_step_text(STWALK_NONE));
    TEST_ASSERT_EQUAL_STRING("OK yes    BACK no", stview_step_keys(STWALK_DISPLAY));
    TEST_ASSERT_EQUAL_STRING("OK yes    BACK no", stview_step_keys(STWALK_LED));
    TEST_ASSERT_EQUAL_STRING("OK heard  BACK not  UP again", stview_step_keys(STWALK_BUZZER));
    TEST_ASSERT_EQUAL_STRING("", stview_step_keys(STWALK_BUTTONS));
    TEST_ASSERT_EQUAL_STRING("", stview_step_keys(STWALK_NONE));
    static const char *const k_names[] = {"UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK"};
    for (uint8_t k = 0; k < 6; k++) {
        TEST_ASSERT_EQUAL_STRING(k_names[k], stview_key_name(k));
    }
    TEST_ASSERT_EQUAL_STRING("?", stview_key_name(6));
    TEST_ASSERT_TRUE(strlen(stview_step_text(STWALK_DISPLAY)) < 40u); /* fits a 320 px line */
    TEST_ASSERT_TRUE(strlen(stview_step_text(STWALK_LED)) < 40u);
}

static void test_the_buttons_prompt_names_the_next_key_and_counts_down(void)
{
    char buf[40];
    stview_buttons_prompt(&w, 1000, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("Press UP  (30 s left)", buf);
    stview_buttons_prompt(&w, 1000 + 1500, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("Press UP  (29 s left)", buf);
    press(0);
    press(1);
    stview_buttons_prompt(&w, 1000 + 29999, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("Press LEFT  (1 s left)", buf);
    stview_buttons_prompt(&w, 1000 + 99999, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("Press LEFT  (0 s left)", buf);
    for (uint8_t k = 2; k < 5; k++) {
        press(k);
    }
    stview_buttons_prompt(&w, 1000, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("Press BACK  (30 s left)", buf);
    w.seen = 0x3F;
    stview_buttons_prompt(&w, 1000, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("All keys seen", buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rows_are_run_summary_the_checks_and_export);
    RUN_TEST(test_labels_and_values);
    RUN_TEST(test_the_screen_summary_is_compact_while_some_checks_are_not_run);
    RUN_TEST(test_results_in_words);
    RUN_TEST(test_buttons_pass_when_every_key_was_pressed_in_any_order);
    RUN_TEST(test_a_key_pressed_twice_counts_once_and_other_actions_count_for_nothing);
    RUN_TEST(test_ok_and_back_are_keys_to_press_in_this_step_not_answers);
    RUN_TEST(test_buttons_time_out_after_30_s_and_name_the_missing_keys);
    RUN_TEST(test_a_timeout_names_every_key_or_just_one);
    RUN_TEST(test_the_timeout_survives_the_clock_wrapping);
    RUN_TEST(test_a_tick_outside_the_buttons_step_does_nothing);
    RUN_TEST(test_a_yes_is_attested_never_pass_and_walks_on);
    RUN_TEST(test_back_is_a_no_that_records_a_failure_and_ends_the_walk);
    RUN_TEST(test_an_answer_needs_its_press_to_have_arrived_in_the_step);
    RUN_TEST(test_the_other_keys_answer_nothing_and_up_replays_the_tones);
    RUN_TEST(test_the_walk_is_not_consumed_before_it_starts);
    RUN_TEST(test_step_texts);
    RUN_TEST(test_the_buttons_prompt_names_the_next_key_and_counts_down);
    return UNITY_END();
}
