/*
 * test_selftest_report.c — the self-test report formatter (TASKS.md STARK-0707): every section and
 * every row present, bounds (a log tail of exactly 4 kB, a total at its cap with the truncation
 * marker inside it), control characters replaced in every field and in the tail, a sink that fails
 * part-way, bad arguments, and the file names.
 */
#include <stdio.h>
#include <string.h>
#include "selftest_report.h"
#include "unity.h"

static char s_out[40000];
static size_t s_len;
static size_t s_fail_after; /* the sink fails once this many bytes were taken (0: never) */
static unsigned s_calls;

static stark_err_t sink(void *ctx, const char *s, size_t n)
{
    (void)ctx;
    s_calls++;
    if (s_fail_after != 0u && s_len + n > s_fail_after) {
        return STARK_ERR_IO;
    }
    memcpy(s_out + s_len, s, n);
    s_len += n;
    s_out[s_len] = '\0';
    return STARK_OK;
}

typedef struct {
    const char *data;
    size_t len;
    unsigned calls;
    size_t over; /* claim this many bytes more than were copied */
} tail_t;

static size_t tail_fn(void *ctx, size_t offset, char *buf, size_t max)
{
    tail_t *t = ctx;
    t->calls++;
    if (offset >= t->len) {
        return 0;
    }
    size_t n = t->len - offset < max ? t->len - offset : max;
    memcpy(buf, t->data + offset, n);
    return n + t->over; /* claims more than it copied: the formatter holds it to what it asked */
}

static stark_diag_snapshot_t s_snap;
static stark_diag_task_t s_tasks[3];
static stark_selftest_row_t s_rows[10];
static const char *s_modules[2] = {"slot 0 absent", "slot 1 known SPI breakout id=0x0b01"};
static selftest_report_src_t s_src;

void setUp(void)
{
    s_len = 0;
    s_out[0] = '\0';
    s_fail_after = 0;
    s_calls = 0;
    memset(&s_snap, 0, sizeof s_snap);
    s_snap.heap_free = 226356;
    s_snap.heap_min = 220000;
    s_snap.heap_largest = 110000;
    s_snap.fps_x10 = 301;
    s_snap.uptime_s = 77;
    s_snap.ev_published = 500;
    memcpy(s_tasks[0].name, "stark_ui", 9);
    s_tasks[0].stack_free = 2400;
    memcpy(s_tasks[1].name, "app_worker", 11);
    s_tasks[1].stack_free = 1800;
    memcpy(s_tasks[2].name, "IDLE0", 6);
    s_tasks[2].stack_free = 700;
    static const char *const k_names[10] = {
        "RAM",     "Flash",   "Settings", "Storage",   "Module bus", "Events and stacks",
        "Buttons", "Display", "Buzzer",   "Status LED"};
    memset(s_rows, 0, sizeof s_rows);
    for (size_t i = 0; i < 10; i++) {
        s_rows[i].name = k_names[i];
        s_rows[i].result = i < 7 ? ST_PASS : ST_ATTESTED;
        s_rows[i].attested = i >= 7;
        snprintf(s_rows[i].evidence, sizeof s_rows[i].evidence, "evidence %u", (unsigned)i);
    }
    s_src = (selftest_report_src_t){.version = "v0.7-3-gabc",
                                    .build = "2026-11-24T10:00:00Z",
                                    .board = "devkitc1",
                                    .uptime_s = 77,
                                    .snap = &s_snap,
                                    .tasks = s_tasks,
                                    .ntasks = 3,
                                    .modules = s_modules,
                                    .nmodules = 2,
                                    .storage = "mounted",
                                    .rows = s_rows,
                                    .nrows = 10};
}
void tearDown(void)
{}

static size_t count(const char *needle)
{
    size_t n = 0;
    for (const char *p = s_out; (p = strstr(p, needle)) != NULL; p += strlen(needle)) {
        n++;
    }
    return n;
}

static void test_every_section_and_every_row_is_present(void)
{
    char tail[] = "first line\nsecond line\n";
    tail_t t = {tail, strlen(tail), 0, 0};
    s_src.tail = tail_fn;
    s_src.tail_ctx = &t;
    size_t written = 0;
    bool truncated = true;
    TEST_ASSERT_EQUAL(STARK_OK,
                      selftest_report_write(&s_src, SELFTEST_REPORT_MAX, SELFTEST_REPORT_TAIL_MAX,
                                            sink, NULL, &written, &truncated));
    TEST_ASSERT_FALSE(truncated);
    TEST_ASSERT_EQUAL_size_t(s_len, written);
    TEST_ASSERT_TRUE(strstr(s_out, "STARK ONE self-test report\n") == s_out);
    TEST_ASSERT_NOT_NULL(strstr(s_out, "firmware: v0.7-3-gabc\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "build: 2026-11-24T10:00:00Z\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "board: devkitc1\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "uptime: 77 s\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "diag: heap=226356 min=220000 largest=110000 fps=30.1 "));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "tasks: 3\n  stark_ui stack free 2400\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "  IDLE0 stack free 700\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "modules: 2\n  slot 0 absent\n  slot 1 known SPI"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "storage: mounted\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "self-test: 10 checks\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "  RAM: pass evidence 0\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "  Display: attested (operator) evidence 7\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "  Status LED: attested (operator) evidence 9\n"));
    for (size_t i = 0; i < 10; i++) {
        char want[40];
        snprintf(want, sizeof want, "  %s: ", s_rows[i].name);
        TEST_ASSERT_EQUAL_size_t(1, count(want));
    }
    TEST_ASSERT_NOT_NULL(strstr(s_out, "summary: 7 pass  3 attested  0 fail  0 skip\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "log tail:\nfirst line\nsecond line\n-- end --\n"));
    TEST_ASSERT_EQUAL_CHAR('\n', s_out[s_len - 1]);
    TEST_ASSERT_TRUE(s_len < SELFTEST_REPORT_MAX);
}

static void test_the_log_tail_is_exactly_four_kilobytes_at_most(void)
{
    static char big[10000];
    for (size_t i = 0; i < sizeof big; i++) {
        big[i] = (i % 64u) == 63u ? '\n' : (char)('a' + (i % 26u));
    }
    tail_t t = {big, sizeof big, 0, 0};
    s_src.tail = tail_fn;
    s_src.tail_ctx = &t;
    TEST_ASSERT_EQUAL(STARK_OK,
                      selftest_report_write(&s_src, SELFTEST_REPORT_MAX, SELFTEST_REPORT_TAIL_MAX,
                                            sink, NULL, NULL, NULL));
    const char *start = strstr(s_out, "log tail:\n") + strlen("log tail:\n");
    const char *end = strstr(start, "-- end --\n");
    TEST_ASSERT_EQUAL_size_t(SELFTEST_REPORT_TAIL_MAX, (size_t)(end - start));
    TEST_ASSERT_EQUAL_MEMORY(big, start, SELFTEST_REPORT_TAIL_MAX);
    /* the same with a provider that hands over more than it was asked for */
    s_len = 0;
    t.over = 500;
    TEST_ASSERT_EQUAL(STARK_OK,
                      selftest_report_write(&s_src, SELFTEST_REPORT_MAX, SELFTEST_REPORT_TAIL_MAX,
                                            sink, NULL, NULL, NULL));
    start = strstr(s_out, "log tail:\n") + strlen("log tail:\n");
    end = strstr(start, "-- end --\n");
    TEST_ASSERT_EQUAL_size_t(SELFTEST_REPORT_TAIL_MAX, (size_t)(end - start));
}

static void test_a_tail_without_a_final_newline_gets_one_and_no_tail_is_a_header_only(void)
{
    tail_t t = {"no newline", 10, 0, 0};
    s_src.tail = tail_fn;
    s_src.tail_ctx = &t;
    TEST_ASSERT_EQUAL(
        STARK_OK, selftest_report_write(&s_src, SELFTEST_REPORT_MAX, 4096, sink, NULL, NULL, NULL));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "log tail:\nno newline\n-- end --\n"));
    s_len = 0;
    s_src.tail = NULL;
    TEST_ASSERT_EQUAL(
        STARK_OK, selftest_report_write(&s_src, SELFTEST_REPORT_MAX, 4096, sink, NULL, NULL, NULL));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "log tail:\n-- end --\n"));
    s_len = 0;
    tail_t empty = {"", 0, 0, 0};
    s_src.tail = tail_fn;
    s_src.tail_ctx = &empty;
    TEST_ASSERT_EQUAL(
        STARK_OK, selftest_report_write(&s_src, SELFTEST_REPORT_MAX, 4096, sink, NULL, NULL, NULL));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "log tail:\n-- end --\n"));
    TEST_ASSERT_EQUAL_UINT(1, empty.calls);
}

static void test_control_characters_become_dots_in_every_field_and_the_tail(void)
{
    s_src.version = "v1\x1b[31m\nfake: line";
    s_src.board = "dev\rkit";
    s_src.storage = "mo\tunted\x7f";
    static const char *mods[1] = {"slot 0 \x07name\nevil"};
    s_src.modules = mods;
    s_src.nmodules = 1;
    memcpy(s_tasks[0].name,
           "a\x01"
           "b",
           4);
    snprintf(s_rows[0].evidence, sizeof s_rows[0].evidence, "bad\x02x");
    s_rows[0].name = "R\nAM";
    char tail[] = "ok\x1b[2Jline\tx\n\x00second\n";
    tail_t t = {tail, sizeof tail - 1u, 0, 0};
    s_src.tail = tail_fn;
    s_src.tail_ctx = &t;
    TEST_ASSERT_EQUAL(
        STARK_OK, selftest_report_write(&s_src, SELFTEST_REPORT_MAX, 4096, sink, NULL, NULL, NULL));
    for (size_t i = 0; i < s_len; i++) {
        unsigned char c = (unsigned char)s_out[i];
        TEST_ASSERT_TRUE(c == '\n' || (c >= 0x20u && c != 0x7fu));
    }
    TEST_ASSERT_NOT_NULL(strstr(s_out, "firmware: v1.[31m.fake: line\n"));
    TEST_ASSERT_NULL(strstr(s_out, "\nfake: line"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "board: dev.kit\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "storage: mo.unted.\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "  slot 0 .name.evil\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "  a.b stack free"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "  R.AM: pass bad.x\n"));
    /* the tail keeps its newlines and loses its other control characters */
    TEST_ASSERT_NOT_NULL(strstr(s_out, "log tail:\nok.[2Jline.x\n"));
}

static void test_missing_pieces_are_marked_not_invented(void)
{
    selftest_report_src_t src = {0};
    TEST_ASSERT_EQUAL(
        STARK_OK, selftest_report_write(&src, SELFTEST_REPORT_MAX, 4096, sink, NULL, NULL, NULL));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "firmware: (none)\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "diag: n/a\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "tasks: 0\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "modules: 0\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "storage: (none)\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "self-test: 0 checks\n"));
    TEST_ASSERT_NULL(strstr(s_out, "summary:"));
    /* a row with no name and none of the optional parts */
    s_len = 0;
    stark_selftest_row_t r = {.result = ST_NOT_RUN};
    src.rows = &r;
    src.nrows = 1;
    TEST_ASSERT_EQUAL(
        STARK_OK, selftest_report_write(&src, SELFTEST_REPORT_MAX, 4096, sink, NULL, NULL, NULL));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "  (none): not run\n"));
    TEST_ASSERT_NOT_NULL(strstr(s_out, "summary: 0 pass  0 attested  0 fail  0 skip  1 not run\n"));
}

static void test_a_report_past_its_cap_ends_with_the_marker_inside_the_cap(void)
{
    static char big[3000];
    memset(big, 'z', sizeof big);
    tail_t t = {big, sizeof big, 0, 0};
    s_src.tail = tail_fn;
    s_src.tail_ctx = &t;
    for (size_t cap = 64; cap < 1400; cap += 37) {
        s_len = 0;
        size_t written = 0;
        bool truncated = false;
        TEST_ASSERT_EQUAL(
            STARK_OK, selftest_report_write(&s_src, cap, 4096, sink, NULL, &written, &truncated));
        TEST_ASSERT_TRUE(truncated);
        TEST_ASSERT_TRUE(written <= cap);
        TEST_ASSERT_EQUAL_size_t(s_len, written);
        TEST_ASSERT_NOT_NULL(strstr(s_out, "-- truncated --\n"));
        TEST_ASSERT_EQUAL_STRING("-- truncated --\n", s_out + s_len - 16u);
    }
    /* a cap that exactly fits is not a truncation */
    s_src.tail = NULL;
    s_len = 0;
    size_t whole = 0;
    TEST_ASSERT_EQUAL(STARK_OK, selftest_report_write(&s_src, SELFTEST_REPORT_MAX, 4096, sink, NULL,
                                                      &whole, NULL));
    for (size_t cap = whole; cap < whole + 40u; cap++) {
        s_len = 0;
        bool truncated = true;
        TEST_ASSERT_EQUAL(STARK_OK,
                          selftest_report_write(&s_src, cap, 4096, sink, NULL, NULL, &truncated));
        TEST_ASSERT_TRUE(s_len <= cap);
        if (cap >= whole + 16u) {
            TEST_ASSERT_FALSE(truncated);
            TEST_ASSERT_EQUAL_size_t(whole, s_len);
        }
    }
}

static void test_the_whole_report_stays_inside_sixteen_kilobytes_by_construction(void)
{
    static char huge[9000];
    memset(huge, 'q', sizeof huge);
    for (size_t i = 79; i < sizeof huge; i += 80) {
        huge[i] = '\n';
    }
    tail_t t = {huge, sizeof huge, 0, 0};
    stark_diag_task_t many[16];
    for (size_t i = 0; i < 16; i++) {
        memcpy(many[i].name, "012345678901234", 16);
        many[i].stack_free = 1000;
    }
    s_src.tasks = many;
    s_src.ntasks = 16;
    s_src.tail = tail_fn;
    s_src.tail_ctx = &t;
    TEST_ASSERT_EQUAL(STARK_OK,
                      selftest_report_write(&s_src, SELFTEST_REPORT_MAX, SELFTEST_REPORT_TAIL_MAX,
                                            sink, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(s_len <= SELFTEST_REPORT_MAX);
    TEST_ASSERT_TRUE(s_len > SELFTEST_REPORT_TAIL_MAX);
}

static void test_a_sink_that_fails_part_way_stops_the_report_and_says_why(void)
{
    char tail[] = "a\nb\nc\n";
    tail_t t = {tail, strlen(tail), 0, 0};
    s_src.tail = tail_fn;
    s_src.tail_ctx = &t;
    size_t whole = 0;
    TEST_ASSERT_EQUAL(STARK_OK, selftest_report_write(&s_src, SELFTEST_REPORT_MAX, 4096, sink, NULL,
                                                      &whole, NULL));
    for (size_t after = 1; after < whole; after += 29) {
        s_len = 0;
        s_calls = 0;
        s_fail_after = after;
        size_t written = 0;
        TEST_ASSERT_EQUAL(STARK_ERR_IO, selftest_report_write(&s_src, SELFTEST_REPORT_MAX, 4096,
                                                              sink, NULL, &written, NULL));
        TEST_ASSERT_TRUE(s_len <= after);
        /* nothing more is attempted once the sink has failed: one failing call, no retries */
        TEST_ASSERT_TRUE(s_calls < 200u);
    }
}

static void test_bad_arguments(void)
{
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      selftest_report_write(NULL, 1000, 10, sink, NULL, NULL, NULL));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      selftest_report_write(&s_src, 1000, 10, NULL, NULL, NULL, NULL));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      selftest_report_write(&s_src, 63, 10, sink, NULL, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT(0, s_calls);
}

static void test_report_names_and_parsing(void)
{
    char name[SELFTEST_REPORT_NAME_MAX];
    TEST_ASSERT_TRUE(selftest_report_name(1, name, sizeof name));
    TEST_ASSERT_EQUAL_STRING("report_0001.txt", name);
    TEST_ASSERT_TRUE(selftest_report_name(9999, name, sizeof name));
    TEST_ASSERT_EQUAL_STRING("report_9999.txt", name);
    TEST_ASSERT_FALSE(selftest_report_name(10000, name, sizeof name));
    TEST_ASSERT_EQUAL_STRING("", name);
    TEST_ASSERT_FALSE(selftest_report_name(1, name, 15));
    TEST_ASSERT_FALSE(selftest_report_name(1, name, 0));
    TEST_ASSERT_FALSE(selftest_report_name(1, NULL, 16));
    unsigned n = 0;
    TEST_ASSERT_TRUE(selftest_report_parse("report_0042.txt", &n));
    TEST_ASSERT_EQUAL_UINT(42, n);
    TEST_ASSERT_TRUE(selftest_report_parse("report_0000.txt", NULL));
    const char *const bad[] = {"report_042.txt",  "report_00042.txt", "report_004a.txt",
                               "Report_0042.txt", "report_0042.tx",   "report_0042.txt ",
                               "report_0042_txt", "stark.log",        "",
                               "report_0042.TXT"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        n = 7;
        TEST_ASSERT_FALSE_MESSAGE(selftest_report_parse(bad[i], &n), bad[i]);
        TEST_ASSERT_EQUAL_UINT(7, n);
    }
    TEST_ASSERT_FALSE(selftest_report_parse(NULL, &n));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_section_and_every_row_is_present);
    RUN_TEST(test_the_log_tail_is_exactly_four_kilobytes_at_most);
    RUN_TEST(test_a_tail_without_a_final_newline_gets_one_and_no_tail_is_a_header_only);
    RUN_TEST(test_control_characters_become_dots_in_every_field_and_the_tail);
    RUN_TEST(test_missing_pieces_are_marked_not_invented);
    RUN_TEST(test_a_report_past_its_cap_ends_with_the_marker_inside_the_cap);
    RUN_TEST(test_the_whole_report_stays_inside_sixteen_kilobytes_by_construction);
    RUN_TEST(test_a_sink_that_fails_part_way_stops_the_report_and_says_why);
    RUN_TEST(test_bad_arguments);
    RUN_TEST(test_report_names_and_parsing);
    return UNITY_END();
}
