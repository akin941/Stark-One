/*
 * test_selftest_export.c — the report written to the card (TASKS.md STARK-0707): the next free
 * file name, every row present, no control character, at most 16 kB, whole or not at all (a card
 * that fails at any step of the write leaves no partial file), read back and compared before it is
 * called good, and nothing sent anywhere (the environment has no way to send: it is a card and
 * nothing else).
 */
#include <stdio.h>
#include <string.h>
#include "selftest_env_fake.h"
#include "selftest_export.h"
#include "selftest_report.h"
#include "unity.h"

static char name[SELFTEST_REPORT_NAME_MAX];

void setUp(void)
{
    fenv_reset();
    memset(name, 'x', sizeof name);
}
void tearDown(void)
{}

static stark_err_t export_now(size_t *bytes)
{
    return selftest_export_run(&fenv_env, name, sizeof name, bytes);
}

static const char *text_of(const fenv_file_t *f)
{
    static char copy[FENV_FILE_BYTES + 1];
    memcpy(copy, f->data, f->len);
    copy[f->len] = '\0';
    return copy;
}

static void test_the_first_report_is_report_0001_and_holds_every_row(void)
{
    size_t bytes = 0;
    TEST_ASSERT_EQUAL(STARK_OK, export_now(&bytes));
    TEST_ASSERT_EQUAL_STRING("report_0001.txt", name);
    fenv_file_t *f = fenv_find("report_0001.txt");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_size_t(f->len, bytes);
    TEST_ASSERT_EQUAL_UINT(1, fenv_file_count()); /* no stray temp file */
    const char *t = text_of(f);
    TEST_ASSERT_TRUE(strstr(t, "STARK ONE self-test report\n") == t);
    TEST_ASSERT_NOT_NULL(strstr(t, "firmware: v0.7-test\n"));
    TEST_ASSERT_NOT_NULL(strstr(t, "board: devkitc1\n"));
    TEST_ASSERT_NOT_NULL(strstr(t, "uptime: 12 s\n"));
    TEST_ASSERT_NOT_NULL(strstr(t, "storage: mounted\n"));
    TEST_ASSERT_NOT_NULL(strstr(t, "tasks: 3\n  stark_ui stack free 2400\n"));
    TEST_ASSERT_NOT_NULL(strstr(t, "modules: 4\n  entry 0 state 0 \n"));
    TEST_ASSERT_NOT_NULL(strstr(t, "self-test: 10 checks\n"));
    for (size_t i = 0; i < fenv.n_rows; i++) {
        char want[96];
        snprintf(want, sizeof want, "  %s: %s%s ev %u\n", fenv.rows[i].name,
                 fenv.rows[i].result == ST_PASS ? "pass" : "attested",
                 fenv.rows[i].attested ? " (operator)" : "", (unsigned)i);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(t, want), want);
    }
    TEST_ASSERT_NOT_NULL(strstr(t, "summary: 7 pass  3 attested  0 fail  0 skip\n"));
    TEST_ASSERT_NOT_NULL(strstr(t, "log tail:\nboot: one\nboot: two\nui: three\n-- end --\n"));
    TEST_ASSERT_EQUAL_UINT(0, fenv.removes);
    TEST_ASSERT_EQUAL_UINT(0, fenv.discards);
}

static void test_a_report_has_no_control_characters_and_fits_sixteen_kilobytes(void)
{
    snprintf(fenv.entries[0].name, sizeof fenv.entries[0].name, "ev\x1b[31m\nil");
    memcpy(fenv.tail, "ok\x1b[2J\x07\r\nline\twith tab\n", 22);
    fenv.tail_len = 22;
    size_t bytes = 0;
    TEST_ASSERT_EQUAL(STARK_OK, export_now(&bytes));
    const fenv_file_t *f = fenv_find(name);
    TEST_ASSERT_NOT_NULL(f);
    for (size_t i = 0; i < f->len; i++) {
        unsigned char c = f->data[i];
        TEST_ASSERT_TRUE(c == '\n' || (c >= 0x20u && c != 0x7fu));
    }
    TEST_ASSERT_TRUE(f->len <= SELFTEST_REPORT_MAX);
    TEST_ASSERT_NOT_NULL(strstr(text_of(f), "ok.[2J..\nline.with tab\n"));
}

static void test_the_log_tail_is_at_most_4_kb_and_the_whole_report_stays_inside_16_kb(void)
{
    size_t n = 0;
    for (size_t line = 0; n + 50u < sizeof fenv.tail; line++) {
        n += (size_t)snprintf(fenv.tail + n, sizeof fenv.tail - n, "line %05u %032u\n",
                              (unsigned)line, (unsigned)line);
    }
    fenv.tail_len = n;
    TEST_ASSERT_TRUE(n > SELFTEST_REPORT_TAIL_MAX);
    fenv.n_tasks = 16;
    for (size_t i = 3; i < 16; i++) {
        snprintf(fenv.tasks[i].name, sizeof fenv.tasks[i].name, "task_%u", (unsigned)i);
        fenv.tasks[i].stack_free = 1000;
    }
    TEST_ASSERT_EQUAL(STARK_OK, export_now(NULL));
    const fenv_file_t *f = fenv_find("report_0001.txt");
    const char *t = text_of(f);
    const char *start = strstr(t, "log tail:\n") + strlen("log tail:\n");
    const char *end = strstr(start, "-- end --\n");
    TEST_ASSERT_TRUE((size_t)(end - start) <=
                     SELFTEST_REPORT_TAIL_MAX + 1u); /* + a final newline */
    TEST_ASSERT_TRUE((size_t)(end - start) > SELFTEST_REPORT_TAIL_MAX - 64u);
    TEST_ASSERT_TRUE(f->len <= SELFTEST_REPORT_MAX);
}

static void test_the_next_name_is_one_past_the_highest_and_other_files_are_ignored(void)
{
    static const char *const k_other[] = {"stark.log", "uart_0003.log", "report_x.txt",
                                          "report_0042.TXT"};
    for (size_t i = 0; i < 4; i++) {
        fenv.files[i].used = true;
        snprintf(fenv.files[i].name, sizeof fenv.files[i].name, "%s", k_other[i]);
    }
    fenv.files[4].used = true;
    snprintf(fenv.files[4].name, sizeof fenv.files[4].name, "report_0007.txt");
    TEST_ASSERT_EQUAL(STARK_OK, export_now(NULL));
    TEST_ASSERT_EQUAL_STRING("report_0008.txt", name);
    TEST_ASSERT_EQUAL(STARK_OK, export_now(NULL));
    TEST_ASSERT_EQUAL_STRING("report_0009.txt", name);
    TEST_ASSERT_NOT_NULL(fenv_find("report_0007.txt")); /* an earlier report is never replaced */
}

static void test_there_is_no_report_after_9999(void)
{
    fenv.files[0].used = true;
    snprintf(fenv.files[0].name, sizeof fenv.files[0].name, "report_9999.txt");
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, export_now(NULL));
    TEST_ASSERT_EQUAL_UINT(0, fenv.opens);
    fenv.files[0].used = true;
    snprintf(fenv.files[0].name, sizeof fenv.files[0].name, "report_9998.txt");
    TEST_ASSERT_EQUAL(STARK_OK, export_now(NULL));
    TEST_ASSERT_EQUAL_STRING("report_9999.txt", name);
}

static void test_without_a_card_nothing_is_written(void)
{
    static const stark_storage_state_t k[] = {STARK_STORAGE_NOT_FITTED, STARK_STORAGE_ABSENT,
                                              STARK_STORAGE_UNUSABLE, STARK_STORAGE_REMOVED,
                                              STARK_STORAGE_EJECTED};
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        fenv.state = k[i];
        TEST_ASSERT_EQUAL(STARK_ERR_STATE, export_now(NULL));
    }
    TEST_ASSERT_EQUAL_UINT(0, fenv.opens);
    TEST_ASSERT_EQUAL_UINT(0, fenv.lists);
    TEST_ASSERT_EQUAL_UINT(0, fenv_file_count());
    TEST_ASSERT_EQUAL_CHAR('x', name[0]); /* the name is untouched */
}

static void test_a_card_that_fails_at_any_write_leaves_no_partial_file(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, export_now(NULL));
    unsigned writes = fenv.writes;
    TEST_ASSERT_TRUE(writes > 20u);
    for (unsigned nth = 0; nth < writes; nth++) {
        fenv_reset();
        fenv.fault = FENV_WRITE;
        fenv.fail_nth = nth;
        TEST_ASSERT_EQUAL_MESSAGE(STARK_ERR_IO, export_now(NULL), "a failed write was swallowed");
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fenv_file_count(), "a partial report was kept");
        TEST_ASSERT_EQUAL_UINT(1, fenv.discards);
        TEST_ASSERT_EQUAL_UINT(0, fenv.closes);
        TEST_ASSERT_EQUAL_UINT(nth + 1u, fenv.writes); /* nothing is written after the failure */
    }
}

static void test_a_card_that_fails_at_open_close_or_listing_leaves_no_file(void)
{
    fenv.fault = FENV_OPEN_WRITE;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, export_now(NULL));
    TEST_ASSERT_EQUAL_UINT(0, fenv_file_count());
    TEST_ASSERT_EQUAL_UINT(0, fenv.writes);
    fenv_reset();
    fenv.fault = FENV_CLOSE;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, export_now(NULL));
    TEST_ASSERT_EQUAL_UINT(0, fenv_file_count());
    TEST_ASSERT_EQUAL_UINT(1, fenv.removes); /* whatever close left behind is removed */
    fenv_reset();
    fenv.fault = FENV_LIST;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, export_now(NULL));
    TEST_ASSERT_EQUAL_UINT(0, fenv.opens);
}

static void test_a_report_that_reads_back_differently_is_removed_and_not_reported_good(void)
{
    static const fenv_card_fault_t k[] = {FENV_BAD_READ, FENV_SHORT_READ, FENV_OPEN_READ,
                                          FENV_READ};
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        fenv_reset();
        fenv.fault = k[i];
        size_t bytes = 77;
        TEST_ASSERT_EQUAL_MESSAGE(STARK_ERR_IO, export_now(&bytes), "a bad read-back was missed");
        TEST_ASSERT_EQUAL_UINT(0, fenv_file_count());
        TEST_ASSERT_EQUAL_UINT(1, fenv.removes);
        TEST_ASSERT_EQUAL_size_t(77, bytes); /* no size is claimed for a report that was not kept */
        TEST_ASSERT_EQUAL_CHAR('x', name[0]);
    }
}

static void test_without_memory_for_the_transient_buffers_nothing_is_attempted(void)
{
    fenv.alloc_fails = true;
    TEST_ASSERT_EQUAL(STARK_ERR_NO_MEM, export_now(NULL));
    TEST_ASSERT_EQUAL_UINT(0, fenv.opens);
    TEST_ASSERT_EQUAL_UINT(0, fenv_file_count());
}

static void test_the_transient_buffers_are_given_back(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, export_now(NULL));
    TEST_ASSERT_EQUAL_UINT(fenv.allocs, fenv.frees);
    TEST_ASSERT_EQUAL_size_t(0, fenv.allocated);
    TEST_ASSERT_TRUE(fenv.last_alloc < 8000u); /* about 5.5 kB, transient */
    fenv_reset();
    fenv.fault = FENV_WRITE;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, export_now(NULL));
    TEST_ASSERT_EQUAL_UINT(fenv.allocs, fenv.frees);
}

static void test_a_missing_snapshot_is_marked_and_the_rest_is_still_written(void)
{
    fenv.snap_fails = true;
    TEST_ASSERT_EQUAL(STARK_OK, export_now(NULL));
    const char *t = text_of(fenv_find(name));
    TEST_ASSERT_NOT_NULL(strstr(t, "diag: n/a\n"));
    TEST_ASSERT_NOT_NULL(strstr(t, "self-test: 10 checks\n"));
}

static void test_more_rows_or_modules_than_the_report_has_room_for_are_clamped(void)
{
    fenv.n_rows =
        STARK_SELFTEST_MAX + 8u; /* the service counts more checks than the report copies */
    memset(fenv.tail, 'q', sizeof fenv.tail);
    fenv.tail_len = sizeof fenv.tail;
    fenv.tail_overreport = 100; /* a provider that claims more than it copied */
    fenv.modules_overreport = 5;
    TEST_ASSERT_EQUAL(STARK_OK, export_now(NULL));
    const fenv_file_t *f = fenv_find(name);
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_TRUE(f->len <= SELFTEST_REPORT_MAX);
    TEST_ASSERT_NOT_NULL(strstr(text_of(f), "modules: 4\n"));
    TEST_ASSERT_NOT_NULL(strstr(text_of(f), "self-test: 12 checks\n"));
}

static void test_bad_arguments(void)
{
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, selftest_export_run(NULL, name, sizeof name, NULL));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, selftest_export_run(&fenv_env, NULL, 16, NULL));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, selftest_export_run(&fenv_env, name, 15, NULL));
    TEST_ASSERT_EQUAL_UINT(0, fenv.opens);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_first_report_is_report_0001_and_holds_every_row);
    RUN_TEST(test_a_report_has_no_control_characters_and_fits_sixteen_kilobytes);
    RUN_TEST(test_the_log_tail_is_at_most_4_kb_and_the_whole_report_stays_inside_16_kb);
    RUN_TEST(test_the_next_name_is_one_past_the_highest_and_other_files_are_ignored);
    RUN_TEST(test_there_is_no_report_after_9999);
    RUN_TEST(test_without_a_card_nothing_is_written);
    RUN_TEST(test_a_card_that_fails_at_any_write_leaves_no_partial_file);
    RUN_TEST(test_a_card_that_fails_at_open_close_or_listing_leaves_no_file);
    RUN_TEST(test_a_report_that_reads_back_differently_is_removed_and_not_reported_good);
    RUN_TEST(test_without_memory_for_the_transient_buffers_nothing_is_attempted);
    RUN_TEST(test_the_transient_buffers_are_given_back);
    RUN_TEST(test_a_missing_snapshot_is_marked_and_the_rest_is_still_written);
    RUN_TEST(test_more_rows_or_modules_than_the_report_has_room_for_are_clamped);
    RUN_TEST(test_bad_arguments);
    return UNITY_END();
}
