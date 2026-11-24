/*
 * test_selftest_core.c — the self-test runner (TASKS.md STARK-0707): ordering, deadlines, results,
 * the summary arithmetic, a check that returns an unknown value, registration limits, a check that
 * overruns its deadline, one that never returned (the contained app faulted), evidence limits and
 * the notifications. The clock is the test's; nothing here sleeps.
 */
#include <stdio.h>
#include <string.h>
#include "selftest_core.h"
#include "unity.h"

static uint32_t s_now;
static unsigned s_locks;
static unsigned s_unlocks;
static unsigned s_notes;
static size_t s_note_index[32];
static stark_selftest_result_t s_note_result[32];
static char s_note_summary[64];
static bool s_note_have_summary;
static selftest_t t;

static uint32_t now_ms(void *ctx)
{
    (void)ctx;
    return s_now;
}
static void lock(void *ctx)
{
    (void)ctx;
    s_locks++;
}
static void unlock(void *ctx)
{
    (void)ctx;
    s_unlocks++;
}
static void notify(void *ctx, size_t index, const stark_selftest_row_t *row, const char *summary)
{
    (void)ctx;
    if (s_notes < 32u) {
        s_note_index[s_notes] = index;
        s_note_result[s_notes] = row->result;
    }
    s_notes++;
    s_note_have_summary = summary != NULL;
    if (summary != NULL) {
        snprintf(s_note_summary, sizeof s_note_summary, "%s", summary);
    }
}

static const selftest_io_t k_io = {now_ms, lock, unlock, notify, NULL};

static stark_selftest_result_t pass_fast(char *ev, size_t cap)
{
    snprintf(ev, cap, "32768 B");
    return ST_PASS;
}
static stark_selftest_result_t fail_fast(char *ev, size_t cap)
{
    snprintf(ev, cap, "bad byte +4");
    return ST_FAIL;
}
static stark_selftest_result_t skip_fast(char *ev, size_t cap)
{
    snprintf(ev, cap, "no card");
    return ST_SKIP;
}
static stark_selftest_result_t slow(char *ev, size_t cap)
{
    s_now += SELFTEST_DEADLINE_MS + 1u;
    snprintf(ev, cap, "late but passing");
    return ST_PASS;
}
static stark_selftest_result_t exactly_on_time(char *ev, size_t cap)
{
    s_now += SELFTEST_DEADLINE_MS;
    snprintf(ev, cap, "on time");
    return ST_PASS;
}
static stark_selftest_result_t unknown_value(char *ev, size_t cap)
{
    snprintf(ev, cap, "who knows");
    return (stark_selftest_result_t)9;
}
static stark_selftest_result_t attested_value(char *ev, size_t cap)
{
    (void)ev;
    (void)cap;
    return ST_ATTESTED; /* a check may not claim an operator's answer */
}
static stark_selftest_result_t not_run_value(char *ev, size_t cap)
{
    (void)ev;
    (void)cap;
    return ST_NOT_RUN;
}
static stark_selftest_result_t noisy(char *ev, size_t cap)
{
    memset(ev, 'x', cap); /* no terminator: the runner may not read past the buffer */
    ev[2] = '\n';
    ev[3] = 0x1b;
    ev[4] = 0x7f;
    return ST_PASS;
}
static stark_selftest_result_t wraps(char *ev, size_t cap)
{
    s_now = 5; /* the clock wrapped between the two readings: elapsed is 10 */
    snprintf(ev, cap, "wrap");
    return ST_PASS;
}

static const stark_selftest_check_t k_table[] = {
    {"RAM", false, pass_fast}, {"Flash", false, fail_fast}, {"Storage", false, skip_fast},
    {"Buttons", false, NULL},  {"Display", true, NULL},
};

void setUp(void)
{
    s_now = 1000;
    s_locks = s_unlocks = s_notes = 0;
    s_note_have_summary = false;
    s_note_summary[0] = '\0';
    selftest_init(&t, &k_io);
}
void tearDown(void)
{}

static void test_registration_limits(void)
{
    stark_selftest_check_t many[STARK_SELFTEST_MAX + 1u];
    for (size_t i = 0; i < STARK_SELFTEST_MAX + 1u; i++) {
        many[i] = (stark_selftest_check_t){"x", false, pass_fast};
    }
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, selftest_register(&t, NULL, 3));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, selftest_register(&t, many, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, selftest_register(&t, many, STARK_SELFTEST_MAX + 1u));
    many[3].name = NULL;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, selftest_register(&t, many, 5));
    many[3].name = "x";
    TEST_ASSERT_EQUAL(STARK_OK, selftest_register(&t, many, STARK_SELFTEST_MAX));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, selftest_register(&t, many, 2)); /* once */
    stark_selftest_row_t rows[STARK_SELFTEST_MAX];
    TEST_ASSERT_EQUAL_size_t(STARK_SELFTEST_MAX, selftest_results(&t, rows, STARK_SELFTEST_MAX));
    TEST_ASSERT_EQUAL(ST_NOT_RUN, rows[0].result);
    TEST_ASSERT_EQUAL_STRING("x", rows[0].name);
}

static void test_before_registration_everything_is_a_state_error(void)
{
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, selftest_run(&t, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, selftest_report_result(&t, 0, ST_PASS, "x"));
    TEST_ASSERT_EQUAL_size_t(0, selftest_results(&t, NULL, 4));
    char sum[64];
    selftest_summary(NULL, 0, sum, sizeof sum);
    TEST_ASSERT_EQUAL_STRING("0 pass  0 attested  0 fail  0 skip", sum);
}

static void test_results_in_order_with_evidence_and_the_summary(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, selftest_register(&t, k_table, 5));
    TEST_ASSERT_EQUAL(STARK_OK, selftest_run(&t, 0));
    TEST_ASSERT_EQUAL(STARK_OK, selftest_run(&t, 1));
    TEST_ASSERT_EQUAL(STARK_OK, selftest_run(&t, 2));
    stark_selftest_row_t rows[8];
    TEST_ASSERT_EQUAL_size_t(5, selftest_results(&t, rows, 8));
    TEST_ASSERT_EQUAL(ST_PASS, rows[0].result);
    TEST_ASSERT_EQUAL_STRING("32768 B", rows[0].evidence);
    TEST_ASSERT_EQUAL(ST_FAIL, rows[1].result);
    TEST_ASSERT_EQUAL_STRING("bad byte +4", rows[1].evidence);
    TEST_ASSERT_EQUAL(ST_SKIP, rows[2].result);
    TEST_ASSERT_EQUAL(ST_NOT_RUN, rows[3].result);
    TEST_ASSERT_TRUE(rows[4].attested);
    TEST_ASSERT_EQUAL_UINT(3, s_notes);
    TEST_ASSERT_EQUAL_size_t(0, s_note_index[0]);
    TEST_ASSERT_EQUAL_size_t(2, s_note_index[2]);
    TEST_ASSERT_FALSE(s_note_have_summary); /* two rows are still not run */
    char sum[64];
    selftest_summary(rows, 5, sum, sizeof sum);
    TEST_ASSERT_EQUAL_STRING("1 pass  0 attested  1 fail  1 skip  2 not run", sum);
    TEST_ASSERT_EQUAL_UINT(s_locks, s_unlocks);
    TEST_ASSERT_TRUE(s_locks > 0);
}

static void test_the_last_result_carries_the_summary_and_an_attestation_never_passes(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, selftest_register(&t, k_table, 5));
    for (size_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(STARK_OK, selftest_run(&t, i));
    }
    TEST_ASSERT_EQUAL(STARK_OK, selftest_report_result(&t, 3, ST_PASS, "all 6 keys seen"));
    TEST_ASSERT_FALSE(s_note_have_summary);
    TEST_ASSERT_EQUAL(STARK_OK, selftest_report_result(&t, 4, ST_ATTESTED, "operator: ok"));
    TEST_ASSERT_TRUE(s_note_have_summary);
    TEST_ASSERT_EQUAL_STRING("2 pass  1 attested  1 fail  1 skip", s_note_summary);
    stark_selftest_row_t rows[8];
    (void)selftest_results(&t, rows, 8);
    TEST_ASSERT_EQUAL(ST_ATTESTED, rows[4].result);
    TEST_ASSERT_EQUAL_UINT(0, rows[4].ms);
}

static void test_a_clear_is_silent_and_a_bad_report_is_refused(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, selftest_register(&t, k_table, 5));
    TEST_ASSERT_EQUAL(STARK_OK, selftest_report_result(&t, 4, ST_ATTESTED, "x"));
    TEST_ASSERT_EQUAL_UINT(1, s_notes);
    TEST_ASSERT_EQUAL(STARK_OK, selftest_report_result(&t, 4, ST_NOT_RUN, ""));
    TEST_ASSERT_EQUAL_UINT(1, s_notes); /* no notification for a clear */
    stark_selftest_row_t rows[5];
    (void)selftest_results(&t, rows, 5);
    TEST_ASSERT_EQUAL(ST_NOT_RUN, rows[4].result);
    TEST_ASSERT_EQUAL_STRING("", rows[4].evidence);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, selftest_report_result(&t, 5, ST_PASS, "x"));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      selftest_report_result(&t, 0, (stark_selftest_result_t)7, "x"));
    TEST_ASSERT_EQUAL_UINT(1, s_notes);
}

static void test_run_refuses_an_unknown_an_interactive_and_an_out_of_range_check(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, selftest_register(&t, k_table, 5));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, selftest_run(&t, 3));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, selftest_run(&t, 5));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, selftest_run(&t, SIZE_MAX));
    TEST_ASSERT_EQUAL_UINT(0, s_notes);
}

static void test_a_check_that_overruns_its_deadline_fails_with_timeout(void)
{
    static const stark_selftest_check_t tab[] = {
        {"slow", false, slow}, {"edge", false, exactly_on_time}, {"wrap", false, wraps}};
    TEST_ASSERT_EQUAL(STARK_OK, selftest_register(&t, tab, 3));
    TEST_ASSERT_EQUAL(STARK_OK, selftest_run(&t, 0));
    TEST_ASSERT_EQUAL(STARK_OK, selftest_run(&t, 1));
    s_now = 0xFFFFFFFBu;
    TEST_ASSERT_EQUAL(STARK_OK, selftest_run(&t, 2));
    stark_selftest_row_t rows[3];
    (void)selftest_results(&t, rows, 3);
    TEST_ASSERT_EQUAL(ST_FAIL, rows[0].result);
    TEST_ASSERT_EQUAL_STRING("timeout", rows[0].evidence);
    TEST_ASSERT_EQUAL_UINT32(SELFTEST_DEADLINE_MS + 1u, rows[0].ms);
    TEST_ASSERT_EQUAL(ST_PASS, rows[1].result); /* exactly at the deadline is still on time */
    TEST_ASSERT_EQUAL_UINT32(SELFTEST_DEADLINE_MS, rows[1].ms);
    TEST_ASSERT_EQUAL(ST_PASS, rows[2].result);
    TEST_ASSERT_EQUAL_UINT32(10, rows[2].ms);
}

static void test_an_unknown_or_forbidden_return_value_is_a_failure(void)
{
    static const stark_selftest_check_t tab[] = {{"unknown", false, unknown_value},
                                                 {"attested", false, attested_value},
                                                 {"notrun", false, not_run_value}};
    TEST_ASSERT_EQUAL(STARK_OK, selftest_register(&t, tab, 3));
    for (size_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(STARK_OK, selftest_run(&t, i));
    }
    stark_selftest_row_t rows[3];
    (void)selftest_results(&t, rows, 3);
    for (size_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(ST_FAIL, rows[i].result);
        TEST_ASSERT_EQUAL_STRING("bad result", rows[i].evidence);
    }
}

static void test_evidence_is_bounded_and_has_no_control_characters(void)
{
    static const stark_selftest_check_t tab[] = {{"noisy", false, noisy}};
    TEST_ASSERT_EQUAL(STARK_OK, selftest_register(&t, tab, 1));
    TEST_ASSERT_EQUAL(STARK_OK, selftest_run(&t, 0));
    stark_selftest_row_t rows[1];
    (void)selftest_results(&t, rows, 1);
    TEST_ASSERT_EQUAL(ST_PASS, rows[0].result);
    TEST_ASSERT_EQUAL_size_t(STARK_SELFTEST_EVIDENCE, strlen(rows[0].evidence));
    TEST_ASSERT_EQUAL_CHAR('x', rows[0].evidence[1]);
    TEST_ASSERT_EQUAL_CHAR('.', rows[0].evidence[2]);
    TEST_ASSERT_EQUAL_CHAR('.', rows[0].evidence[3]);
    TEST_ASSERT_EQUAL_CHAR('.', rows[0].evidence[4]);
    /* a reported string is cut the same way, and NULL is an empty one */
    char longer[120];
    memset(longer, 'y', sizeof longer);
    longer[sizeof longer - 1u] = '\0';
    TEST_ASSERT_EQUAL(STARK_OK, selftest_report_result(&t, 0, ST_PASS, longer));
    (void)selftest_results(&t, rows, 1);
    TEST_ASSERT_EQUAL_size_t(STARK_SELFTEST_EVIDENCE, strlen(rows[0].evidence));
    TEST_ASSERT_EQUAL(STARK_OK, selftest_report_result(&t, 0, ST_PASS, NULL));
    (void)selftest_results(&t, rows, 1);
    TEST_ASSERT_EQUAL_STRING("", rows[0].evidence);
}

static void test_a_run_that_never_returned_is_recorded_as_an_app_fault_by_the_next_run(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, selftest_register(&t, k_table, 5));
    TEST_ASSERT_EQUAL(STARK_OK, selftest_run(&t, 0));
    t.in_flight = 1; /* the app was stopped inside check 1: it never recorded anything */
    stark_selftest_row_t rows[5];
    (void)selftest_results(&t, rows, 5);
    TEST_ASSERT_EQUAL(ST_PASS, rows[0].result); /* the table keeps what finished */
    TEST_ASSERT_EQUAL(ST_NOT_RUN, rows[1].result);
    TEST_ASSERT_EQUAL(STARK_OK, selftest_run(&t, 2));
    (void)selftest_results(&t, rows, 5);
    TEST_ASSERT_EQUAL(ST_FAIL, rows[1].result);
    TEST_ASSERT_EQUAL_STRING("app_fault", rows[1].evidence);
    TEST_ASSERT_EQUAL(ST_SKIP, rows[2].result);
    TEST_ASSERT_EQUAL_size_t(SELFTEST_NONE, t.in_flight);
}

static void test_results_copies_at_most_max_and_reports_the_total(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, selftest_register(&t, k_table, 5));
    stark_selftest_row_t rows[2];
    memset(rows, 0x55, sizeof rows);
    TEST_ASSERT_EQUAL_size_t(5, selftest_results(&t, rows, 2));
    TEST_ASSERT_EQUAL_STRING("RAM", rows[0].name);
    TEST_ASSERT_EQUAL_STRING("Flash", rows[1].name);
    TEST_ASSERT_EQUAL_size_t(5, selftest_results(&t, NULL, 0));
}

static void test_summary_arithmetic_and_limits(void)
{
    stark_selftest_row_t rows[12];
    memset(rows, 0, sizeof rows);
    for (size_t i = 0; i < 12; i++) {
        rows[i].result = (stark_selftest_result_t)(i % 5u);
    }
    char sum[64];
    selftest_summary(rows, 12, sum, sizeof sum);
    TEST_ASSERT_EQUAL_STRING("3 pass  2 attested  2 fail  2 skip  3 not run", sum);
    selftest_summary(rows, 12, sum, 8); /* truncated, still terminated */
    TEST_ASSERT_EQUAL_size_t(7, strlen(sum));
    selftest_summary(rows, 12, sum, 1);
    TEST_ASSERT_EQUAL_size_t(0, strlen(sum));
    selftest_summary(rows, 12, NULL, 8);
    selftest_summary(rows, 12, sum, 0);
    rows[0].result = (stark_selftest_result_t)9; /* an unknown value counts as nothing */
    selftest_summary(rows, 1, sum, sizeof sum);
    TEST_ASSERT_EQUAL_STRING("0 pass  0 attested  0 fail  0 skip", sum);
    /* a table of 7 pass, 3 attested: the spec's example */
    for (size_t i = 0; i < 10; i++) {
        rows[i].result = i < 7 ? ST_PASS : ST_ATTESTED;
    }
    selftest_summary(rows, 10, sum, sizeof sum);
    TEST_ASSERT_EQUAL_STRING("7 pass  3 attested  0 fail  0 skip", sum);
}

static void test_result_names(void)
{
    TEST_ASSERT_EQUAL_STRING("not run", selftest_result_name(ST_NOT_RUN));
    TEST_ASSERT_EQUAL_STRING("pass", selftest_result_name(ST_PASS));
    TEST_ASSERT_EQUAL_STRING("fail", selftest_result_name(ST_FAIL));
    TEST_ASSERT_EQUAL_STRING("skip", selftest_result_name(ST_SKIP));
    TEST_ASSERT_EQUAL_STRING("attested", selftest_result_name(ST_ATTESTED));
    TEST_ASSERT_EQUAL_STRING("not run", selftest_result_name((stark_selftest_result_t)9));
}

static void test_a_runner_without_a_lock_or_a_notifier_works(void)
{
    selftest_io_t bare = {.now_ms = now_ms};
    selftest_init(&t, &bare);
    TEST_ASSERT_EQUAL(STARK_OK, selftest_register(&t, k_table, 5));
    TEST_ASSERT_EQUAL(STARK_OK, selftest_run(&t, 0));
    TEST_ASSERT_EQUAL(STARK_OK, selftest_report_result(&t, 4, ST_ATTESTED, "x"));
    stark_selftest_row_t rows[5];
    TEST_ASSERT_EQUAL_size_t(5, selftest_results(&t, rows, 5));
    TEST_ASSERT_EQUAL(ST_PASS, rows[0].result);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_registration_limits);
    RUN_TEST(test_before_registration_everything_is_a_state_error);
    RUN_TEST(test_results_in_order_with_evidence_and_the_summary);
    RUN_TEST(test_the_last_result_carries_the_summary_and_an_attestation_never_passes);
    RUN_TEST(test_a_clear_is_silent_and_a_bad_report_is_refused);
    RUN_TEST(test_run_refuses_an_unknown_an_interactive_and_an_out_of_range_check);
    RUN_TEST(test_a_check_that_overruns_its_deadline_fails_with_timeout);
    RUN_TEST(test_an_unknown_or_forbidden_return_value_is_a_failure);
    RUN_TEST(test_evidence_is_bounded_and_has_no_control_characters);
    RUN_TEST(test_a_run_that_never_returned_is_recorded_as_an_app_fault_by_the_next_run);
    RUN_TEST(test_results_copies_at_most_max_and_reports_the_total);
    RUN_TEST(test_summary_arithmetic_and_limits);
    RUN_TEST(test_result_names);
    RUN_TEST(test_a_runner_without_a_lock_or_a_notifier_works);
    return UNITY_END();
}
