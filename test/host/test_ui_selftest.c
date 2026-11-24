/*
 * test_ui_selftest.c — the real Self-Test app on the host UI port (TASKS.md STARK-0707) over the
 * real runner and service (selftest_core.c, selftest_svc.c: the real log lines and events) and a
 * table of fake checks: the home list before and after a run, Run all executing the automated
 * checks on the (inline) worker and then walking the operator through the keys, the colour bars,
 * the tones and the LED, a yes that is `attested` and never `pass`, BACK on a question recording a
 * failure and ending the walk, the 30 s of the Buttons step, a failing check in the summary, OK on
 * a row showing its evidence, the export with and without a card, a second Run all clearing the
 * first silently, an empty or short table, a worker that cannot start, and leaving mid-walk.
 * Frames pinned after inspection.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_input.h"
#include "stark_ui.h"
#include "ui_port.h"
#include "ui_port_selftest.h"
#include "unity.h"

extern const stark_app_t app_selftest;
static const stark_app_t *const k_apps[] = {&app_selftest};
static bool s_ui_ready;

static stark_err_t s_export_err;
static size_t s_export_bytes;
static unsigned s_exports;

static stark_err_t fake_export(char *name, size_t cap, size_t *bytes)
{
    s_exports++;
    if (s_export_err == STARK_OK) {
        (void)snprintf(name, cap, "report_0001.txt");
        *bytes = s_export_bytes;
    }
    return s_export_err;
}

void setUp(void)
{
    hal_host_set_now_us(0);
    ui_port_worker_inline(true);
    ui_port_selftest_reset();
    TEST_ASSERT_EQUAL(STARK_OK, ui_port_selftest_register(10));
    stark_selftest_set_export(fake_export);
    s_exports = 0;
    s_export_err = STARK_OK;
    s_export_bytes = 1432;
    if (!s_ui_ready) {
        TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
        TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
        TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
        TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, 1));
        s_ui_ready = true;
    }
}

void tearDown(void)
{
    (void)stark_ui_pop_to_root(); /* a failed test leaves no app running for the next one */
    ui_port_worker_inline(false);
}

static void frame(void)
{
    stark_ui_tick();
}

/* One key, then two frames: a tick dispatches the events queued before it, so what the key's own
 * handling published (the worker's notification, a result) is seen by the second. */
static void press(stark_key_t k)
{
    ui_port_short(k);
    frame();
    frame();
}

/* A frame pinned after inspection; PIN_COLLECT=1 prints every hash instead of asserting. */
static void pin(const char *name, uint64_t want)
{
    uint64_t h = ui_port_fb_hash();
    bool collect = getenv("PIN_COLLECT") != NULL;
    if (h != want || collect) {
        char path[64];
        snprintf(path, sizeof path, "ui_selftest_%s.ppm", name);
        (void)ui_port_dump_ppm(path);
        printf("selftest %s hash 0x%016llx (see %s)\n", name, (unsigned long long)h, path);
    }
    if (!collect) {
        TEST_ASSERT_EQUAL_HEX64(want, h);
    }
}

static unsigned count_logs(const char *needle)
{
    unsigned n = 0;
    for (size_t i = 0; i < ui_port_log_count(); i++) {
        n += strstr(ui_port_log_line(i), needle) != NULL ? 1u : 0u;
    }
    return n;
}

static void start(void)
{
    ui_port_reset_records();
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("selftest"));
    frame();
}

static void diag_sample(void)
{
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_DIAG_SAMPLE;
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_publish(&e));
    frame();
}

/* Run all (the selection starts on it): the automated checks, then the walk begins. */
static void run_all(void)
{
    press(STARK_KEY_OK);
}

static void press_all_keys(void)
{
    static const stark_key_t k[] = {STARK_KEY_UP,    STARK_KEY_DOWN, STARK_KEY_LEFT,
                                    STARK_KEY_RIGHT, STARK_KEY_OK,   STARK_KEY_BACK};
    for (size_t i = 0; i < 6; i++) {
        press(k[i]);
    }
}

static void answer_yes_to_all(void)
{
    press(STARK_KEY_OK); /* display */
    press(STARK_KEY_OK); /* buzzer */
    press(STARK_KEY_OK); /* LED */
}

static stark_selftest_row_t row(size_t i)
{
    stark_selftest_row_t rows[STARK_SELFTEST_MAX];
    (void)stark_selftest_results(rows, STARK_SELFTEST_MAX);
    return rows[i];
}

static void test_home_lists_run_the_summary_every_check_and_export(void)
{
    start();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: start selftest"));
    TEST_ASSERT_TRUE(ui_port_log_contains("selftestapp: home"));
    pin("home_fresh", 0xa28d8ea747557b19ULL);
    TEST_ASSERT_EQUAL_UINT(0, count_logs("selftest: "));
}

static void test_run_all_runs_the_automated_checks_then_walks_the_operator_through_the_rest(void)
{
    start();
    run_all();
    TEST_ASSERT_TRUE(ui_port_log_contains("selftestapp: run all"));
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: ram pass 32768 B"));
    TEST_ASSERT_TRUE(
        ui_port_log_contains("selftest: flash pass 4 MB, image 556784 B, sha 1a2b3c4d"));
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: settings pass 4 settings valid, from nvs"));
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: storage skip no card"));
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: module bus pass table agrees with probes"));
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: events and stacks pass drops 0"));
    for (size_t i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_UINT(1, ui_port_selftest_runs(i)); /* each one ran once, in order */
    }
    TEST_ASSERT_TRUE(ui_port_log_contains("selftestapp: walk start"));
    pin("walk_buttons", 0xb2e35f30d036af75ULL);
    press(STARK_KEY_UP);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_LEFT);
    pin("walk_buttons_3", 0x51529be8f8896810ULL);
    press(STARK_KEY_RIGHT);
    press(STARK_KEY_OK);
    TEST_ASSERT_FALSE(ui_port_log_contains("selftest: buttons"));
    press(STARK_KEY_BACK);
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: buttons pass all 6 keys seen"));
    /* BACK was the last key: its release and short press did not answer the display question */
    TEST_ASSERT_FALSE(ui_port_log_contains("selftest: display"));
    pin("walk_display", 0xa401c93bfde23422ULL);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: display attested operator: bars and ramp"));
    TEST_ASSERT_EQUAL_INT(1, ui_port_buzzer_plays()); /* the three tones start with the step */
    pin("walk_buzzer", 0x24082ceca8f090ffULL);
    press(STARK_KEY_UP);
    TEST_ASSERT_EQUAL_INT(2, ui_port_buzzer_plays()); /* UP plays them again */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: buzzer attested operator: heard three tones"));
    pin("walk_led", 0xb540c187fcce5585ULL);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: status led attested operator: blinks"));
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: summary 6 pass  3 attested  0 fail  1 skip"));
    TEST_ASSERT_TRUE(ui_port_log_contains("selftestapp: walk done"));
    TEST_ASSERT_FALSE(ui_port_log_contains("selftest: display pass"));
    TEST_ASSERT_FALSE(ui_port_log_contains("selftest: buzzer pass"));
    TEST_ASSERT_FALSE(ui_port_log_contains("selftest: status led pass"));
    pin("home_done", 0xc8341e3dc8be58c8ULL);
    /* what the service holds: the attested are attested, and exactly six passed */
    unsigned passed = 0;
    for (size_t i = 0; i < 10; i++) {
        passed += row(i).result == ST_PASS ? 1u : 0u;
    }
    TEST_ASSERT_EQUAL_UINT(6, passed);
    TEST_ASSERT_EQUAL(ST_ATTESTED, row(7).result);
    TEST_ASSERT_EQUAL(ST_ATTESTED, row(8).result);
    TEST_ASSERT_EQUAL(ST_ATTESTED, row(9).result);
}

static void test_back_on_a_question_records_a_failure_and_ends_the_walk(void)
{
    start();
    run_all();
    press_all_keys();
    press(STARK_KEY_BACK); /* "the display does not look right" */
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: display fail operator: not right"));
    TEST_ASSERT_TRUE(ui_port_log_contains("selftestapp: walk ended"));
    TEST_ASSERT_FALSE(ui_port_log_contains("selftest: summary")); /* two rows are still not run */
    TEST_ASSERT_EQUAL(ST_FAIL, row(7).result);
    TEST_ASSERT_EQUAL(ST_NOT_RUN, row(8).result);
    TEST_ASSERT_EQUAL(ST_NOT_RUN, row(9).result);
    TEST_ASSERT_EQUAL_INT(0, ui_port_buzzer_plays()); /* the tones were never played */
    pin("home_ended", 0x14b54ab49788b339ULL);
}

static void test_back_on_the_buzzer_or_the_led_records_that_one_and_leaves_the_rest(void)
{
    start();
    run_all();
    press_all_keys();
    press(STARK_KEY_OK);
    press(STARK_KEY_BACK);
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: buzzer fail operator: heard nothing"));
    TEST_ASSERT_EQUAL(ST_ATTESTED, row(7).result);
    TEST_ASSERT_EQUAL(ST_FAIL, row(8).result);
    TEST_ASSERT_EQUAL(ST_NOT_RUN, row(9).result);
    press(STARK_KEY_BACK); /* leave the app */
    start();
    run_all();
    press_all_keys();
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    press(STARK_KEY_BACK);
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: status led fail operator: no blink"));
    TEST_ASSERT_EQUAL(ST_FAIL, row(9).result);
}

static void test_the_buttons_step_ends_after_30_seconds_naming_the_missing_keys(void)
{
    start();
    run_all();
    press(STARK_KEY_UP);
    press(STARK_KEY_DOWN);
    hal_host_set_now_us(29u * 1000000u);
    diag_sample();
    TEST_ASSERT_FALSE(ui_port_log_contains("selftest: buttons"));
    pin("walk_buttons_wait", 0x371153c56e30da70ULL);
    hal_host_set_now_us(30u * 1000000u);
    diag_sample();
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: buttons fail missing LEFT,RIGHT,OK,BACK"));
    TEST_ASSERT_EQUAL(ST_FAIL, row(6).result);
    pin("walk_display_after_timeout", 0xa401c93bfde23422ULL); /* the questions go on */
    answer_yes_to_all();
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: summary 5 pass  3 attested  1 fail  1 skip"));
}

static void test_a_failing_check_shows_fail_and_the_summary_counts_it(void)
{
    ui_port_selftest_outcome(3, ST_FAIL, "write failed: I/O error");
    start();
    run_all();
    press_all_keys();
    answer_yes_to_all();
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: storage fail write failed: I/O error"));
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: summary 6 pass  3 attested  1 fail  0 skip"));
    pin("home_fail", 0xca3525a6da83ec42ULL);
}

static void test_ok_on_a_row_shows_its_result_and_evidence(void)
{
    start();
    run_all();
    press_all_keys();
    answer_yes_to_all();
    press(STARK_KEY_DOWN); /* RAM: Run all is row 0, the summary row 1 is never selected */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"RAM\""));
    pin("alert_ram", 0xf5bf9f0dde0b4937ULL);
    press(STARK_KEY_OK);
    press(STARK_KEY_DOWN); /* Flash */
    press(STARK_KEY_DOWN); /* Settings */
    press(STARK_KEY_DOWN); /* Storage: skipped, with the state as its evidence */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Storage\""));
    press(STARK_KEY_OK);
    TEST_ASSERT_EQUAL_UINT(0, ui_port_selftest_runs(0) - 1u); /* looking ran nothing again */
}

static void test_a_row_that_was_not_run_says_so(void)
{
    start();
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"RAM\""));
    pin("alert_not_run", 0x4b306f7caa5f5702ULL);
    TEST_ASSERT_EQUAL_UINT(0, ui_port_selftest_runs(0));
}

static void test_export_without_a_card_says_so_and_logs_it(void)
{
    s_export_err = STARK_ERR_STATE;
    start();
    press(STARK_KEY_UP); /* wraps to the last row: Export report */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("selftestapp: export"));
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: export skipped (no card)"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"No card\""));
    pin("alert_no_card", 0x74fdcc4210e07e68ULL);
    TEST_ASSERT_EQUAL_UINT(1, s_exports);
}

static void test_export_writes_the_report_and_names_it(void)
{
    start();
    press(STARK_KEY_UP);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: export report_0001.txt 1432 B"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Report saved\""));
    pin("alert_saved", 0x7e555032892a91ecULL);
}

static void test_export_that_fails_names_the_error(void)
{
    s_export_err = STARK_ERR_IO;
    start();
    press(STARK_KEY_UP);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: export failed: I/O error"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Export failed\""));
    TEST_ASSERT_FALSE(ui_port_log_contains("export skipped"));
}

static void test_a_second_run_clears_the_first_silently(void)
{
    start();
    run_all();
    press_all_keys();
    answer_yes_to_all();
    TEST_ASSERT_EQUAL_UINT(1, count_logs("selftest: summary"));
    ui_port_selftest_outcome(3, ST_FAIL, "open failed: I/O error");
    run_all();
    TEST_ASSERT_EQUAL_UINT(2, ui_port_selftest_runs(0));
    TEST_ASSERT_EQUAL_UINT(1, count_logs("selftest: storage fail open failed"));
    press_all_keys();
    press(STARK_KEY_BACK); /* the display does not look right this time */
    TEST_ASSERT_EQUAL(ST_FAIL, row(3).result);
    TEST_ASSERT_EQUAL(ST_FAIL, row(7).result);
    TEST_ASSERT_EQUAL(ST_NOT_RUN, row(8).result); /* the old yes is gone, not carried over */
    TEST_ASSERT_EQUAL(ST_NOT_RUN, row(9).result);
    TEST_ASSERT_EQUAL_UINT(1, count_logs("selftest: summary")); /* clearing logged nothing */
}

static void test_a_second_press_on_run_all_while_it_runs_does_nothing(void)
{
    start();
    ui_port_short(STARK_KEY_OK);
    ui_port_short(STARK_KEY_OK); /* queued before the worker's notification is seen */
    frame();
    TEST_ASSERT_EQUAL_UINT(1, ui_port_selftest_runs(0));
    TEST_ASSERT_EQUAL_UINT(1, count_logs("selftestapp: run all"));
}

static void test_with_no_checks_registered_run_all_says_so(void)
{
    ui_port_selftest_reset(); /* an empty service */
    start();
    run_all();
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Self-Test\""));
    TEST_ASSERT_FALSE(ui_port_log_contains("selftestapp: run all"));
    pin("home_empty", 0xc6a247df563c1bb1ULL);
}

static void test_a_table_without_the_interactive_checks_has_no_walk(void)
{
    ui_port_selftest_reset();
    TEST_ASSERT_EQUAL(STARK_OK, ui_port_selftest_register(4));
    start();
    run_all();
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: storage skip no card"));
    TEST_ASSERT_FALSE(ui_port_log_contains("selftestapp: walk start"));
    TEST_ASSERT_TRUE(ui_port_log_contains("selftest: summary 3 pass  0 attested  0 fail  1 skip"));
}

static void test_a_worker_that_cannot_start_is_reported_and_nothing_runs(void)
{
    ui_port_worker_inline(false);
    start();
    run_all();
    TEST_ASSERT_TRUE(ui_port_log_contains("selftestapp: worker not started"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Self-Test\""));
    TEST_ASSERT_EQUAL_UINT(0, ui_port_selftest_runs(0));
    press(STARK_KEY_OK); /* the alert */
    press(STARK_KEY_UP);
    press(STARK_KEY_OK); /* Export: the same */
    TEST_ASSERT_EQUAL_UINT(0, s_exports);
}

static void test_leaving_mid_walk_stops_the_tones_and_the_app(void)
{
    start();
    run_all();
    press_all_keys();
    press(STARK_KEY_OK); /* display: the tones start */
    int stops = ui_port_buzzer_stops();
    ui_port_key(STARK_KEY_BACK, STARK_KEY_PRESS);
    ui_port_key(STARK_KEY_BACK, STARK_KEY_LONG); /* held: home from anywhere */
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop selftest"));
    TEST_ASSERT_TRUE(ui_port_buzzer_stops() > stops);
    ui_port_key(STARK_KEY_BACK, STARK_KEY_RELEASE);
    frame();
    /* the app starts again from nothing and the old walk is gone */
    start();
    TEST_ASSERT_FALSE(ui_port_log_contains("selftestapp: walk"));
    TEST_ASSERT_EQUAL(ST_ATTESTED, row(7).result); /* what finished before leaving is kept */
    TEST_ASSERT_EQUAL(ST_NOT_RUN, row(9).result);
}

static void test_a_check_row_is_never_a_way_to_run_it_and_the_summary_row_is_never_selected(void)
{
    start();
    press(STARK_KEY_DOWN);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_UP);
    press(STARK_KEY_UP); /* back on Run all, the summary skipped both ways */
    TEST_ASSERT_TRUE(ui_port_log_contains("menu: sel=0 \"Run all\""));
    TEST_ASSERT_FALSE(ui_port_log_contains("menu: sel=1"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_home_lists_run_the_summary_every_check_and_export);
    RUN_TEST(test_run_all_runs_the_automated_checks_then_walks_the_operator_through_the_rest);
    RUN_TEST(test_back_on_a_question_records_a_failure_and_ends_the_walk);
    RUN_TEST(test_back_on_the_buzzer_or_the_led_records_that_one_and_leaves_the_rest);
    RUN_TEST(test_the_buttons_step_ends_after_30_seconds_naming_the_missing_keys);
    RUN_TEST(test_a_failing_check_shows_fail_and_the_summary_counts_it);
    RUN_TEST(test_ok_on_a_row_shows_its_result_and_evidence);
    RUN_TEST(test_a_row_that_was_not_run_says_so);
    RUN_TEST(test_export_without_a_card_says_so_and_logs_it);
    RUN_TEST(test_export_writes_the_report_and_names_it);
    RUN_TEST(test_export_that_fails_names_the_error);
    RUN_TEST(test_a_second_run_clears_the_first_silently);
    RUN_TEST(test_a_second_press_on_run_all_while_it_runs_does_nothing);
    RUN_TEST(test_with_no_checks_registered_run_all_says_so);
    RUN_TEST(test_a_table_without_the_interactive_checks_has_no_walk);
    RUN_TEST(test_a_worker_that_cannot_start_is_reported_and_nothing_runs);
    RUN_TEST(test_leaving_mid_walk_stops_the_tones_and_the_app);
    RUN_TEST(test_a_check_row_is_never_a_way_to_run_it_and_the_summary_row_is_never_selected);
    return UNITY_END();
}
