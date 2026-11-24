/*
 * test_uart_log.c — the UART app's log to the card (apps/app_uart/uart_log.c, TASKS.md STARK-0705,
 * ADR-0023 §7) over an in-memory stark_storage (test/host/support/storage_fake.c): numbering from
 * the files that are there, the 2 kB buffer and its flushes, a sync (a close and a reopen) every 4
 * kB and a flush every second a byte waits, the 1 MiB limit exactly, a card that goes away mid-log,
 * every failure the card can report, and the CRC-32 the log reports against the bytes on the
 * "card". Proves the rules, never a byte on a real card (ROADMAP V1 exit 13).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "stark_log.h"
#include "storage_fake.h"
#include "uartapp.h"
#include "unity.h"

/* The log lines, captured. */
static char s_lines[64][120];
static size_t s_n_lines;

esp_log_level_t esp_log_level_get(const char *tag)
{
    (void)tag;
    return ESP_LOG_INFO;
}

void esp_log_write(esp_log_level_t level, const char *tag, const char *format, ...)
{
    (void)level;
    (void)tag;
    if (s_n_lines < 64u) {
        va_list ap;
        va_start(ap, format);
        (void)vsnprintf(s_lines[s_n_lines], sizeof s_lines[0], format, ap);
        va_end(ap);
        s_lines[s_n_lines][strcspn(s_lines[s_n_lines], "\n")] = '\0';
        s_n_lines++;
    }
}

static bool logged(const char *needle)
{
    for (size_t i = 0; i < s_n_lines; i++) {
        if (strstr(s_lines[i], needle) != NULL) {
            return true;
        }
    }
    return false;
}

static uint8_t s_pattern[4096];

void setUp(void)
{
    sfake_reset();
    s_n_lines = 0;
    for (size_t i = 0; i < sizeof s_pattern; i++) {
        s_pattern[i] = (uint8_t)(i * 31u + 7u);
    }
    uartlog_stop(); /* nothing from the previous test is left to log */
    sfake_reset();
    s_n_lines = 0;
}

void tearDown(void)
{
    uartlog_stop();
    TEST_ASSERT_EQUAL_UINT(0, sfake_open_handles());
}

/* Writes `total` bytes of the pattern in pieces of `piece`, the clock at `now`. */
static void feed(size_t total, size_t piece, uint32_t now)
{
    size_t done = 0;
    while (done < total) {
        size_t n = total - done < piece ? total - done : piece;
        uartlog_write(s_pattern + (done % 2048u), n, now);
        done += n;
    }
}

static uint32_t crc_of_fed(size_t total, size_t piece)
{
    uint32_t crc = 0;
    size_t done = 0;
    while (done < total) {
        size_t n = total - done < piece ? total - done : piece;
        crc = stark_lab_uart_crc32(crc, s_pattern + (done % 2048u), n);
        done += n;
    }
    return crc;
}

/* The file on the "card" has the length the log counted and the CRC-32 it reported: the log's own
 * numbers against an independent pass over what is on the card. */
static void assert_file_matches_the_log(const char *name, size_t total)
{
    size_t len = 0;
    const uint8_t *data = sfake_get(STARK_DIR_LOGS, name, &len);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_UINT(total, len);
    TEST_ASSERT_EQUAL_HEX32(uartlog_crc(), stark_lab_uart_crc32(0, data, len));
}

/* The file on the "card" equals the bytes fed by one feed(), byte for byte. */
static void assert_file_is_the_fed_bytes(const char *name, size_t total, size_t piece)
{
    size_t len = 0;
    const uint8_t *data = sfake_get(STARK_DIR_LOGS, name, &len);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_UINT(total, len);
    size_t done = 0;
    while (done < total) {
        size_t n = total - done < piece ? total - done : piece;
        TEST_ASSERT_EQUAL_UINT8_ARRAY(s_pattern + (done % 2048u), data + done, n);
        done += n;
    }
}

static void test_without_a_card_it_refuses_with_the_reason(void)
{
    sfake_set_state(STARK_STORAGE_ABSENT);
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, uartlog_start());
    TEST_ASSERT_TRUE(logged("log refused: no card"));
    TEST_ASSERT_EQUAL(UARTLOG_OFF, uartlog_state());
    TEST_ASSERT_EQUAL_UINT(0, sfake_files(STARK_DIR_LOGS));
    uartlog_write(s_pattern, 10, 0); /* nothing is taken */
    TEST_ASSERT_EQUAL_UINT32(0, uartlog_bytes());
    sfake_set_state(STARK_STORAGE_UNUSABLE);
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, uartlog_start());
    TEST_ASSERT_TRUE(logged("log refused: unusable"));
}

static void test_the_next_free_number_is_chosen_from_the_files_there(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    TEST_ASSERT_EQUAL_STRING("uart_0001.log", uartlog_name());
    uartlog_stop();
    sfake_put(STARK_DIR_LOGS, "uart_0007.log", s_pattern, 4);
    sfake_put(STARK_DIR_LOGS, "uart_0003.log", s_pattern, 4);
    sfake_put(STARK_DIR_LOGS, "uart_abcd.log", s_pattern, 4);
    sfake_put(STARK_DIR_LOGS, "uart_0100.txt", s_pattern, 4);
    sfake_put(STARK_DIR_LOGS, "stark_0099.log", s_pattern, 4);
    sfake_put(STARK_DIR_CAPTURES, "uart_0500.log", s_pattern, 4); /* another directory */
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    TEST_ASSERT_EQUAL_STRING("uart_0008.log", uartlog_name());
    TEST_ASSERT_TRUE(logged("log started uart_0008.log"));
    TEST_ASSERT_EQUAL(UARTLOG_ON, uartlog_state());
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, uartlog_start()); /* one at a time */
    uartlog_stop();
    /* a full set of numbers is the end of them */
    sfake_put(STARK_DIR_LOGS, "uart_9999.log", s_pattern, 4);
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, uartlog_start());
    TEST_ASSERT_EQUAL(UARTLOG_FAILED, uartlog_state());
}

/* AC 3: the buffer is 2 kB and nothing reaches the card before it is full or a second has passed.
 */
static void test_bytes_wait_in_the_buffer_until_it_is_full_or_a_second_passed(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    uartlog_write(s_pattern, 100, 5000);
    uartlog_write(s_pattern + 100, 100, 5100);
    TEST_ASSERT_EQUAL_UINT(0, sfake_writes());
    uartlog_tick(5999); /* the first byte has waited 999 ms */
    TEST_ASSERT_EQUAL_UINT(0, sfake_writes());
    uartlog_tick(6000);
    TEST_ASSERT_EQUAL_UINT(1, sfake_writes());
    size_t len = 0;
    (void)sfake_get(STARK_DIR_LOGS, "uart_0001.log", &len);
    TEST_ASSERT_EQUAL_UINT(200, len);
    uartlog_tick(9000); /* nothing waits: nothing to write */
    TEST_ASSERT_EQUAL_UINT(1, sfake_writes());
    /* exactly the buffer: written at once */
    uartlog_write(s_pattern, 2048, 20000);
    TEST_ASSERT_EQUAL_UINT(2, sfake_writes());
    /* one byte short of the buffer: waits */
    uartlog_write(s_pattern, 2047, 21000);
    TEST_ASSERT_EQUAL_UINT(2, sfake_writes());
    uartlog_write(s_pattern, 1, 21000);
    TEST_ASSERT_EQUAL_UINT(3, sfake_writes());
    TEST_ASSERT_EQUAL_UINT32(200u + 2048u + 2048u, uartlog_bytes());
    uartlog_stop();
}

static void test_stopping_writes_the_rest_and_reports_the_crc_of_exactly_the_bytes(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    feed(5000, 37, 100);
    uint32_t crc = crc_of_fed(5000, 37);
    TEST_ASSERT_EQUAL_UINT32(5000, uartlog_bytes());
    TEST_ASSERT_EQUAL_HEX32(crc, uartlog_crc());
    uartlog_stop();
    assert_file_is_the_fed_bytes("uart_0001.log", 5000, 37);
    char want[64];
    snprintf(want, sizeof want, "log closed uart_0001.log 5000 bytes crc 0x%08x", (unsigned)crc);
    TEST_ASSERT_TRUE(logged(want));
    TEST_ASSERT_EQUAL(UARTLOG_OFF, uartlog_state());
    /* the report stays readable, and a second stop adds nothing */
    size_t lines = s_n_lines;
    uartlog_stop();
    TEST_ASSERT_EQUAL_UINT(lines, s_n_lines);
    TEST_ASSERT_EQUAL_UINT32(5000, uartlog_bytes());
    TEST_ASSERT_EQUAL_STRING("uart_0001.log", uartlog_name());
    /* the CRC of a known string is the published one */
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    uartlog_write((const uint8_t *)"123456789", 9, 0);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, uartlog_crc());
    uartlog_stop();
    TEST_ASSERT_EQUAL_STRING("uart_0002.log", uartlog_name());
}

static void test_a_second_log_is_a_new_file_and_the_first_is_untouched(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    feed(300, 100, 0);
    uartlog_stop();
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    feed(50, 50, 0);
    uartlog_stop();
    assert_file_is_the_fed_bytes("uart_0001.log", 300, 100);
    size_t len = 0;
    (void)sfake_get(STARK_DIR_LOGS, "uart_0002.log", &len);
    TEST_ASSERT_EQUAL_UINT(50, len);
}

/* AC 3: the card is synced every 4 kB (a close, which the storage service syncs on). */
static void test_the_medium_is_synced_every_4_kb_and_on_stop(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    TEST_ASSERT_EQUAL_UINT(0, sfake_closes());
    feed(4095, 100, 0); /* one flush of 2048 so far */
    TEST_ASSERT_EQUAL_UINT(0, sfake_closes());
    feed(1, 1, 0); /* the second flush: 4096 written since the start */
    TEST_ASSERT_EQUAL_UINT(1, sfake_closes());
    TEST_ASSERT_EQUAL_UINT(1, sfake_open_handles()); /* reopened for append */
    feed(4096, 64, 0);
    TEST_ASSERT_EQUAL_UINT(2, sfake_closes());
    feed(1000, 100, 0);
    TEST_ASSERT_EQUAL_UINT(2, sfake_closes());
    uartlog_stop();
    TEST_ASSERT_EQUAL_UINT(3, sfake_closes()); /* stopping syncs the rest */
    assert_file_matches_the_log("uart_0001.log", 4096u + 4096u + 1000u);
}

/* AC 3: at 1 MiB it stops, says so, and takes nothing more. */
static void test_the_log_stops_at_one_mebibyte_exactly(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    size_t total = 1024u * 1024u + 100u;
    feed(total, 61, 0);
    TEST_ASSERT_EQUAL(UARTLOG_FULL, uartlog_state());
    TEST_ASSERT_EQUAL_UINT32(1024u * 1024u, uartlog_bytes());
    TEST_ASSERT_TRUE(logged("log full uart_0001.log 1048576 bytes"));
    TEST_ASSERT_EQUAL_UINT(0, sfake_open_handles()); /* closed at the limit: the card can go */
    assert_file_is_the_fed_bytes("uart_0001.log", 1024u * 1024u, 61);
    TEST_ASSERT_EQUAL_HEX32(crc_of_fed(1024u * 1024u, 61), uartlog_crc());
    assert_file_matches_the_log("uart_0001.log", 1024u * 1024u);
    uartlog_write(s_pattern, 10, 0); /* ignored */
    uartlog_tick(99999);
    TEST_ASSERT_EQUAL_UINT32(1024u * 1024u, uartlog_bytes());
    uartlog_stop();
    TEST_ASSERT_TRUE(logged("log closed uart_0001.log 1048576 bytes"));
    TEST_ASSERT_EQUAL(UARTLOG_FULL,
                      uartlog_state()); /* the monitor keeps showing that it is full */
    /* and a new log starts over, as the next file */
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    TEST_ASSERT_EQUAL_STRING("uart_0002.log", uartlog_name());
    TEST_ASSERT_EQUAL(UARTLOG_ON, uartlog_state());
}

static void test_a_log_that_ends_exactly_at_the_limit_is_full_too(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    feed(1024u * 1024u, 2048, 0);
    TEST_ASSERT_EQUAL(UARTLOG_FULL, uartlog_state());
    TEST_ASSERT_EQUAL_UINT32(1024u * 1024u, uartlog_bytes());
    assert_file_is_the_fed_bytes("uart_0001.log", 1024u * 1024u, 2048);
}

static void test_a_tail_in_the_buffer_at_the_limit_is_written_before_the_close(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    feed(1000, 100, 0);
    uartlog_tick(5000); /* a time flush moves the buffer's phase: it will not end on a boundary */
    feed(1024u * 1024u, 100, 6000);
    TEST_ASSERT_EQUAL(UARTLOG_FULL, uartlog_state());
    TEST_ASSERT_EQUAL_UINT32(1024u * 1024u, uartlog_bytes());
    assert_file_matches_the_log("uart_0001.log", 1024u * 1024u);
    TEST_ASSERT_EQUAL_UINT(0, sfake_open_handles());
    /* the same with a card that refuses that last write */
    uartlog_stop();
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    feed(1000, 100, 0);
    uartlog_tick(5000);
    feed(1024u * 1024u - 1000u - 100u, 2048, 6000);
    sfake_fail(SFAKE_WRITE, 0);
    feed(100, 100, 6000); /* the tail is 1048 bytes: its flush fails */
    TEST_ASSERT_EQUAL(UARTLOG_FAILED, uartlog_state());
    sfake_fail(SFAKE_WRITE, -1);
}

static void test_a_card_that_goes_away_mid_log_stops_the_log_and_loses_only_the_buffer(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    feed(3000, 100, 0); /* 2048 written, 952 in the buffer */
    sfake_set_state(STARK_STORAGE_REMOVED);
    feed(1200, 100, 0); /* the next flush hits a card that is gone */
    TEST_ASSERT_EQUAL(UARTLOG_FAILED, uartlog_state());
    TEST_ASSERT_TRUE(logged("log failed:"));
    TEST_ASSERT_EQUAL_UINT(0, sfake_open_handles());
    uint32_t bytes = uartlog_bytes();
    uartlog_write(s_pattern, 10, 0); /* nothing more is taken */
    TEST_ASSERT_EQUAL_UINT32(bytes, uartlog_bytes());
    uartlog_stop(); /* the summary is still said once, and the state stays */
    TEST_ASSERT_TRUE(logged("log closed uart_0001.log"));
    TEST_ASSERT_EQUAL(UARTLOG_FAILED, uartlog_state());
    sfake_set_state(STARK_STORAGE_MOUNTED);
    size_t len = 0;
    (void)sfake_get(STARK_DIR_LOGS, "uart_0001.log", &len);
    TEST_ASSERT_EQUAL_UINT(2048, len);            /* what reached the card before it went */
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start()); /* and a new log can start on the next card */
    TEST_ASSERT_EQUAL_STRING("uart_0002.log", uartlog_name());
}

static void test_a_failing_write_a_failing_sync_and_a_failing_stop(void)
{
    /* a write fails */
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    sfake_fail(SFAKE_WRITE, 0);
    feed(2048, 64, 0);
    TEST_ASSERT_EQUAL(UARTLOG_FAILED, uartlog_state());
    uartlog_stop();
    sfake_fail(SFAKE_WRITE, -1);
    /* the sync's close fails */
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    sfake_fail(SFAKE_CLOSE, 0);
    feed(4096, 64, 0);
    TEST_ASSERT_EQUAL(UARTLOG_FAILED, uartlog_state());
    uartlog_stop();
    sfake_fail(SFAKE_CLOSE, -1);
    /* the sync's reopen fails */
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    sfake_fail(SFAKE_OPEN, 0);
    feed(4096, 64, 0);
    TEST_ASSERT_EQUAL(UARTLOG_FAILED, uartlog_state());
    uartlog_stop();
    sfake_fail(SFAKE_OPEN, -1);
    /* the flush at stop fails */
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    feed(100, 100, 0);
    sfake_fail(SFAKE_WRITE, 0);
    uartlog_stop();
    TEST_ASSERT_EQUAL(UARTLOG_FAILED, uartlog_state());
    sfake_fail(SFAKE_WRITE, -1);
    /* the close at stop fails: it is said, the state is off */
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    feed(100, 100, 0);
    sfake_fail(SFAKE_CLOSE, 0);
    s_n_lines = 0;
    uartlog_stop();
    TEST_ASSERT_TRUE(logged("log close failed:"));
    TEST_ASSERT_EQUAL(UARTLOG_OFF, uartlog_state());
    sfake_fail(SFAKE_CLOSE, -1);
    /* the flush at the limit fails */
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start());
    feed(1024u * 1024u - 100u, 2048, 0);
    sfake_fail(SFAKE_WRITE, 0);
    feed(100, 100, 0);
    TEST_ASSERT_EQUAL(UARTLOG_FAILED, uartlog_state());
}

static void test_a_log_that_cannot_start_says_why(void)
{
    sfake_fail(SFAKE_LIST, 0);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, uartlog_start());
    TEST_ASSERT_EQUAL(UARTLOG_FAILED, uartlog_state());
    TEST_ASSERT_TRUE(logged("log not started"));
    sfake_fail(SFAKE_LIST, -1);
    sfake_fail(SFAKE_OPEN, 0);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, uartlog_start());
    sfake_fail(SFAKE_OPEN, -1);
    TEST_ASSERT_EQUAL(STARK_OK, uartlog_start()); /* and it works once the card does */
    uartlog_stop();
    uartlog_stop(); /* a stop with nothing started is nothing */
}

static void test_stop_with_no_log_started_is_nothing(void)
{
    uartlog_stop();
    TEST_ASSERT_EQUAL_UINT(0, s_n_lines);
    uartlog_tick(1000);
    uartlog_write(s_pattern, 4, 0);
    TEST_ASSERT_EQUAL_UINT32(0, uartlog_bytes());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_without_a_card_it_refuses_with_the_reason);
    RUN_TEST(test_the_next_free_number_is_chosen_from_the_files_there);
    RUN_TEST(test_bytes_wait_in_the_buffer_until_it_is_full_or_a_second_passed);
    RUN_TEST(test_stopping_writes_the_rest_and_reports_the_crc_of_exactly_the_bytes);
    RUN_TEST(test_a_second_log_is_a_new_file_and_the_first_is_untouched);
    RUN_TEST(test_the_medium_is_synced_every_4_kb_and_on_stop);
    RUN_TEST(test_the_log_stops_at_one_mebibyte_exactly);
    RUN_TEST(test_a_log_that_ends_exactly_at_the_limit_is_full_too);
    RUN_TEST(test_a_tail_in_the_buffer_at_the_limit_is_written_before_the_close);
    RUN_TEST(test_a_card_that_goes_away_mid_log_stops_the_log_and_loses_only_the_buffer);
    RUN_TEST(test_a_failing_write_a_failing_sync_and_a_failing_stop);
    RUN_TEST(test_a_log_that_cannot_start_says_why);
    RUN_TEST(test_stop_with_no_log_started_is_nothing);
    return UNITY_END();
}
