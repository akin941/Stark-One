/*
 * test_uart_views.c — the UART app's rows and texts (apps/app_uart/uart_views.c, TASKS.md
 * STARK-0705): the home list, the monitor's header and its windows into the ring (rows aligned to
 * the newest byte, every scroll position), the counts, the Send view's rows and byte editor (every
 * byte reachable from every other by the nibble steps) and the texts of the failures. Pure.
 */
#include <stdio.h>
#include <string.h>
#include "uartapp.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static void test_the_home_list(void)
{
    static const char *const k_label[] = {"Monitor", "Send", "Loopback test", "Auto-baud",
                                          "Log",     "Baud", "Format"};
    char buf[16];
    TEST_ASSERT_EQUAL_UINT(7, UARTHOME_ROWS);
    for (size_t i = 0; i < UARTHOME_ROWS; i++) {
        TEST_ASSERT_EQUAL_STRING(k_label[i], uartview_home_label(i));
    }
    TEST_ASSERT_EQUAL_STRING("", uartview_home_label(UARTHOME_ROWS));
    TEST_ASSERT_NULL(uartview_home_value(UARTHOME_MONITOR, false, 7, 0, "", "", buf, sizeof buf));
    TEST_ASSERT_NULL(uartview_home_value(UARTHOME_SEND, false, 7, 0, "", "", buf, sizeof buf));
    TEST_ASSERT_NULL(uartview_home_value(UARTHOME_ROWS, false, 7, 0, "", "", buf, sizeof buf));
    TEST_ASSERT_NULL(uartview_home_value(UARTHOME_LOOP, false, 7, 0, "", "", buf, sizeof buf));
    TEST_ASSERT_NULL(uartview_home_value(UARTHOME_LOOP, false, 7, 0, NULL, "", buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("64/64 ok", uartview_home_value(UARTHOME_LOOP, false, 7, 0, "64/64 ok",
                                                             "", buf, sizeof buf));
    TEST_ASSERT_NULL(uartview_home_value(UARTHOME_AUTOBAUD, false, 7, 0, "", "", buf, sizeof buf));
    TEST_ASSERT_NULL(
        uartview_home_value(UARTHOME_AUTOBAUD, false, 7, 0, "", NULL, buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING(
        "38400", uartview_home_value(UARTHOME_AUTOBAUD, false, 7, 0, "", "38400", buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING(
        "off", uartview_home_value(UARTHOME_LOG, false, 7, 0, "", "", buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING(
        "on", uartview_home_value(UARTHOME_LOG, true, 7, 0, "", "", buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING(
        "115200", uartview_home_value(UARTHOME_BAUD, false, 7, 0, "", "", buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING(
        "1200", uartview_home_value(UARTHOME_BAUD, false, 0, 0, "", "", buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING(
        "7E1", uartview_home_value(UARTHOME_FORMAT, false, 7, 4, "", "", buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING(
        "", uartview_home_value(UARTHOME_FORMAT, false, 7, 9, "", "", buf, sizeof buf));
}

static void test_a_list_step_stops_at_its_ends(void)
{
    size_t i = 0;
    TEST_ASSERT_FALSE(uartview_step(&i, 11, -1));
    TEST_ASSERT_EQUAL_UINT(0, i);
    TEST_ASSERT_FALSE(uartview_step(&i, 11, 0));
    for (int k = 1; k <= 10; k++) {
        TEST_ASSERT_TRUE(uartview_step(&i, 11, 1));
        TEST_ASSERT_EQUAL_UINT((size_t)k, i);
    }
    TEST_ASSERT_FALSE(uartview_step(&i, 11, 1));
    TEST_ASSERT_EQUAL_UINT(10, i);
    TEST_ASSERT_TRUE(uartview_step(&i, 11, -1));
    TEST_ASSERT_EQUAL_UINT(9, i);
}

static void test_counts_are_compact(void)
{
    char b[12];
    uartview_count_text(0, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("0", b);
    uartview_count_text(9999, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("9999", b);
    uartview_count_text(10000, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("10.0k", b);
    uartview_count_text(123456, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("123.4k", b);
    uartview_count_text(999999, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("999.9k", b);
    uartview_count_text(1000000, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("1.0M", b);
    uartview_count_text(1234567, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("1.2M", b);
    uartview_count_text(0xFFFFFFFFu, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("4294.9M", b);
}

static void test_the_monitor_header(void)
{
    char b[64];
    uartview_settings_text(7, 0, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("115200 8N1", b);
    uartview_settings_text(0, 4, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("1200 7E1", b);
    uartview_settings_text(0, 9, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("1200 ", b);
    stark_lab_uart_stats_t st = {.rx = 1234};
    uartview_stats_text(&st, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("rx 1234  err 0  lost 0", b);
    st = (stark_lab_uart_stats_t){
        .rx = 123456, .framing = 3, .parity = 4, .overrun = 5, .lost = 20000};
    uartview_stats_text(&st, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("rx 123.4k  err 12  lost 20.0k", b);
    st = (stark_lab_uart_stats_t){.framing = 0xFFFFFFFFu, .parity = 0xFFFFFFFFu};
    uartview_stats_text(&st, b, sizeof b); /* the sum does not wrap */
    TEST_ASSERT_EQUAL_STRING("rx 0  err 4294.9M  lost 0", b);
    TEST_ASSERT_TRUE(strlen(b) <= 40); /* it fits the 40 columns of the 8x16 font */
}

static void test_rows_are_aligned_to_the_newest_byte(void)
{
    TEST_ASSERT_EQUAL_UINT(16, uartview_per_row(true));
    TEST_ASSERT_EQUAL_UINT(48, uartview_per_row(false));
    TEST_ASSERT_EQUAL_UINT(0, uartview_total_rows(true, 0));
    TEST_ASSERT_EQUAL_UINT(1, uartview_total_rows(true, 1));
    TEST_ASSERT_EQUAL_UINT(1, uartview_total_rows(true, 16));
    TEST_ASSERT_EQUAL_UINT(2, uartview_total_rows(true, 17));
    TEST_ASSERT_EQUAL_UINT(256, uartview_total_rows(true, 4096));
    TEST_ASSERT_EQUAL_UINT(86, uartview_total_rows(false, 4096));
    size_t skip = 99;
    size_t len = 99;
    /* 40 bytes as hex: rows end at 40, 24, 8: the oldest row holds the odd 8 */
    uartview_row_span(true, 40, 0, &skip, &len);
    TEST_ASSERT_EQUAL_UINT(0, skip);
    TEST_ASSERT_EQUAL_UINT(8, len);
    uartview_row_span(true, 40, 1, &skip, &len);
    TEST_ASSERT_EQUAL_UINT(8, skip);
    TEST_ASSERT_EQUAL_UINT(16, len);
    uartview_row_span(true, 40, 2, &skip, &len);
    TEST_ASSERT_EQUAL_UINT(24, skip);
    TEST_ASSERT_EQUAL_UINT(16, len);
    uartview_row_span(true, 40, 3, &skip, &len); /* past the end */
    TEST_ASSERT_EQUAL_UINT(40, skip);
    TEST_ASSERT_EQUAL_UINT(0, len);
    /* an exact multiple has no odd row */
    uartview_row_span(true, 32, 0, &skip, &len);
    TEST_ASSERT_EQUAL_UINT(0, skip);
    TEST_ASSERT_EQUAL_UINT(16, len);
    /* every byte is in exactly one row, for every count */
    for (size_t count = 0; count <= 200; count++) {
        for (int hex = 0; hex <= 1; hex++) {
            size_t total = uartview_total_rows(hex != 0, count);
            size_t next = 0;
            for (size_t r = 0; r < total; r++) {
                uartview_row_span(hex != 0, count, r, &skip, &len);
                TEST_ASSERT_EQUAL_UINT(next, skip);
                TEST_ASSERT_TRUE(len >= 1u && len <= uartview_per_row(hex != 0));
                next = skip + len;
            }
            TEST_ASSERT_EQUAL_UINT(count, next);
        }
    }
}

static void test_the_window_and_the_scroll(void)
{
    size_t first = 99;
    TEST_ASSERT_EQUAL_UINT(0, uartview_window(true, 0, 0, 17, &first));
    TEST_ASSERT_EQUAL_UINT(0, first);
    /* fewer rows than the screen: all of them, whatever the scroll */
    TEST_ASSERT_EQUAL_UINT(3, uartview_window(true, 40, 0, 17, &first));
    TEST_ASSERT_EQUAL_UINT(0, first);
    TEST_ASSERT_EQUAL_UINT(3, uartview_window(true, 40, 5, 17, &first));
    TEST_ASSERT_EQUAL_UINT(0, first);
    /* 4 kB as hex: 256 rows, the newest 17 at the bottom */
    TEST_ASSERT_EQUAL_UINT(17, uartview_window(true, 4096, 0, 17, &first));
    TEST_ASSERT_EQUAL_UINT(239, first);
    TEST_ASSERT_EQUAL_UINT(17, uartview_window(true, 4096, 10, 17, &first));
    TEST_ASSERT_EQUAL_UINT(229, first);
    TEST_ASSERT_EQUAL_UINT(239, uartview_max_scroll(true, 4096, 17));
    TEST_ASSERT_EQUAL_UINT(17, uartview_window(true, 4096, 239, 17, &first));
    TEST_ASSERT_EQUAL_UINT(0, first);
    TEST_ASSERT_EQUAL_UINT(17, uartview_window(true, 4096, 5000, 17, &first)); /* clamped */
    TEST_ASSERT_EQUAL_UINT(0, first);
    TEST_ASSERT_EQUAL_UINT(0, uartview_max_scroll(true, 40, 17));
    TEST_ASSERT_EQUAL_UINT(0, uartview_max_scroll(true, 0, 17));
    /* the scroll itself */
    TEST_ASSERT_EQUAL_UINT(1, uartview_scroll(0, 1, 10));
    TEST_ASSERT_EQUAL_UINT(10, uartview_scroll(9, 5, 10));
    TEST_ASSERT_EQUAL_UINT(10, uartview_scroll(10, 1, 10));
    TEST_ASSERT_EQUAL_UINT(0, uartview_scroll(3, -5, 10));
    TEST_ASSERT_EQUAL_UINT(2, uartview_scroll(3, -1, 10));
    TEST_ASSERT_EQUAL_UINT(0, uartview_scroll(0, -1, 10));
    TEST_ASSERT_EQUAL_UINT(0, uartview_scroll(0, 3, 0));
}

static void test_row_text(void)
{
    const uint8_t b[] = {0x00, 0x41, 0x7E, 0x7F, 0xFF, 0x20};
    char buf[64];
    uartview_row_text(true, b, sizeof b, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("00 41 7e 7f ff 20", buf);
    uartview_row_text(false, b, sizeof b, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING(".A~.. ", buf);
    /* a full hex row fits the 6-pixel font's 53 columns: 16 bytes are 47 characters */
    uint8_t row[16];
    memset(row, 0xAB, sizeof row);
    uartview_row_text(true, row, sizeof row, buf, sizeof buf);
    TEST_ASSERT_EQUAL_UINT(47, strlen(buf));
    uint8_t wide[48];
    memset(wide, 'x', sizeof wide);
    uartview_row_text(false, wide, sizeof wide, buf, sizeof buf);
    TEST_ASSERT_EQUAL_UINT(48, strlen(buf));
}

static void test_the_status_line(void)
{
    char b[64];
    uartview_status_text(UARTLOG_OFF, "", 0, 0, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("", b);
    uartview_status_text(UARTLOG_OFF, NULL, 0, 4, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("back 4", b);
    uartview_status_text(UARTLOG_ON, "uart_0001.log", 1234, 0, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("Log uart_0001.log 1234", b);
    uartview_status_text(UARTLOG_ON, NULL, 12345, 12, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("Log  12.3k  back 12", b);
    uartview_status_text(UARTLOG_FULL, "uart_0001.log", 1048576, 0, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("Log full", b);
    uartview_status_text(UARTLOG_FAILED, "uart_0001.log", 10, 2, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("Log stopped  back 2", b);
    char tiny[6];
    uartview_status_text(UARTLOG_ON, "uart_0001.log", 1, 9, tiny, sizeof tiny); /* never overruns */
    TEST_ASSERT_EQUAL_UINT(5, strlen(tiny));
}

static void test_failure_texts(void)
{
    TEST_ASSERT_EQUAL_STRING("pins in use", uartview_error_text(STARK_ERR_BUSY));
    TEST_ASSERT_EQUAL_STRING("pins not offered", uartview_error_text(STARK_ERR_NOT_SUPPORTED));
    TEST_ASSERT_EQUAL_STRING("no memory", uartview_error_text(STARK_ERR_NO_MEM));
    TEST_ASSERT_EQUAL_STRING("not ready", uartview_error_text(STARK_ERR_STATE));
    TEST_ASSERT_EQUAL_STRING("failed", uartview_error_text(STARK_ERR_IO));
    TEST_ASSERT_EQUAL_STRING("failed", uartview_error_text(STARK_ERR_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("failed", uartview_error_text(STARK_ERR_NO_SPACE));
    TEST_ASSERT_EQUAL_STRING("failed", uartview_error_text(STARK_ERR_NOT_FOUND));
    TEST_ASSERT_EQUAL_STRING("failed", uartview_error_text(STARK_ERR_INVALID_ARG));
    TEST_ASSERT_EQUAL_STRING("failed", uartview_error_text(STARK_OK));
}

static void test_the_canned_sends(void)
{
    size_t n = 99;
    const uint8_t *b = uartview_canned(0, &n);
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_EQUAL_UINT8(0x0D, b[0]);
    TEST_ASSERT_EQUAL_UINT8(0x0A, b[1]);
    TEST_ASSERT_EQUAL_STRING("\\r\\n", uartview_canned_label(0));
    b = uartview_canned(1, &n);
    TEST_ASSERT_EQUAL_UINT(4, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("AT\r\n", b, 4);
    TEST_ASSERT_EQUAL_STRING("AT\\r\\n", uartview_canned_label(1));
    b = uartview_canned(2, &n);
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("?\r\n", b, 3);
    b = uartview_canned(3, &n);
    TEST_ASSERT_EQUAL_UINT(1, n);
    TEST_ASSERT_EQUAL_UINT8(0x03, b[0]);
    TEST_ASSERT_EQUAL_STRING("Ctrl-C (03)", uartview_canned_label(3));
    b = uartview_canned(4, &n);
    TEST_ASSERT_EQUAL_UINT(1, n);
    TEST_ASSERT_EQUAL_UINT8(0x04, b[0]);
    TEST_ASSERT_NULL(uartview_canned(5, &n));
    TEST_ASSERT_EQUAL_UINT(0, n);
    TEST_ASSERT_EQUAL_STRING("", uartview_canned_label(5));
    for (size_t i = 0; i < UARTAPP_CANNED; i++) { /* every canned send fits one send */
        (void)uartview_canned(i, &n);
        TEST_ASSERT_TRUE(n >= 1u && n <= STARK_LAB_UART_SEND_MAX);
    }
}

static void test_the_send_rows(void)
{
    uartapp_send_cfg_t c;
    uartrow_t rows[UARTAPP_ROWS_MAX];
    uartview_send_cfg_reset(&c);
    TEST_ASSERT_EQUAL_UINT8(1, c.count);
    TEST_ASSERT_FALSE(c.low_nibble);
    size_t n = uartview_send_rows(&c, rows);
    TEST_ASSERT_EQUAL_UINT(5 + 1 + 3 + 1, n); /* canned, file, Bytes, Nibble, Byte 0, Send */
    for (size_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(UARTROW_CANNED, rows[i].kind);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, rows[i].idx);
    }
    TEST_ASSERT_EQUAL(UARTROW_FILE, rows[5].kind);
    TEST_ASSERT_EQUAL(UARTROW_COUNT, rows[6].kind);
    TEST_ASSERT_EQUAL(UARTROW_NIBBLE, rows[7].kind);
    TEST_ASSERT_EQUAL(UARTROW_BYTE, rows[8].kind);
    TEST_ASSERT_EQUAL(UARTROW_SEND, rows[9].kind);
    c.count = 32;
    n = uartview_send_rows(&c, rows);
    TEST_ASSERT_EQUAL_UINT(UARTAPP_ROWS_MAX, n);
    TEST_ASSERT_EQUAL(UARTROW_BYTE, rows[n - 2].kind);
    TEST_ASSERT_EQUAL_UINT8(31, rows[n - 2].idx);
    TEST_ASSERT_EQUAL(UARTROW_SEND, rows[n - 1].kind);
    c.count = 99; /* a corrupt count cannot overrun the rows */
    n = uartview_send_rows(&c, rows);
    TEST_ASSERT_EQUAL_UINT(UARTAPP_ROWS_MAX, n);
}

static void test_the_send_labels_and_values(void)
{
    uartapp_send_cfg_t c;
    char val[16];
    uartview_send_cfg_reset(&c);
    c.count = 4;
    c.data[2] = 0xAB;
    uartrow_t canned = {UARTROW_CANNED, 1};
    uartrow_t file = {UARTROW_FILE, 0};
    uartrow_t count = {UARTROW_COUNT, 0};
    uartrow_t nibble = {UARTROW_NIBBLE, 0};
    uartrow_t byte = {UARTROW_BYTE, 2};
    uartrow_t send = {UARTROW_SEND, 0};
    uartrow_t far = {UARTROW_BYTE, 40};
    TEST_ASSERT_EQUAL_STRING("AT\\r\\n", uartview_send_label(&canned));
    TEST_ASSERT_EQUAL_STRING("File uart_send.txt", uartview_send_label(&file));
    TEST_ASSERT_EQUAL_STRING("Bytes", uartview_send_label(&count));
    TEST_ASSERT_EQUAL_STRING("Nibble", uartview_send_label(&nibble));
    TEST_ASSERT_EQUAL_STRING("Byte 2", uartview_send_label(&byte));
    TEST_ASSERT_EQUAL_STRING("Byte 31", uartview_send_label(&(uartrow_t){UARTROW_BYTE, 31}));
    TEST_ASSERT_EQUAL_STRING("Byte ?", uartview_send_label(&far));
    TEST_ASSERT_EQUAL_STRING("Send bytes", uartview_send_label(&send));
    TEST_ASSERT_NULL(uartview_send_value(&canned, &c, NULL, val, sizeof val));
    TEST_ASSERT_NULL(uartview_send_value(&file, &c, "", val, sizeof val));
    TEST_ASSERT_NULL(uartview_send_value(&send, &c, NULL, val, sizeof val));
    TEST_ASSERT_EQUAL_STRING("4", uartview_send_value(&count, &c, NULL, val, sizeof val));
    TEST_ASSERT_EQUAL_STRING("high", uartview_send_value(&nibble, &c, NULL, val, sizeof val));
    c.low_nibble = true;
    TEST_ASSERT_EQUAL_STRING("low", uartview_send_value(&nibble, &c, NULL, val, sizeof val));
    TEST_ASSERT_EQUAL_STRING("ab", uartview_send_value(&byte, &c, NULL, val, sizeof val));
    TEST_ASSERT_EQUAL_STRING("00", uartview_send_value(&far, &c, NULL, val, sizeof val));
    /* a status text takes the place of the value */
    TEST_ASSERT_EQUAL_STRING("sent 4", uartview_send_value(&file, &c, "sent 4", val, sizeof val));
    TEST_ASSERT_EQUAL_STRING("sent 4", uartview_send_value(&byte, &c, "sent 4", val, sizeof val));
}

static void test_the_editor_adjusts_only_what_it_should(void)
{
    uartapp_send_cfg_t c;
    uartview_send_cfg_reset(&c);
    uartrow_t canned = {UARTROW_CANNED, 0};
    uartrow_t file = {UARTROW_FILE, 0};
    uartrow_t send = {UARTROW_SEND, 0};
    uartrow_t count = {UARTROW_COUNT, 0};
    uartrow_t nibble = {UARTROW_NIBBLE, 0};
    TEST_ASSERT_FALSE(uartview_send_adjust(&c, &canned, 1));
    TEST_ASSERT_FALSE(uartview_send_adjust(&c, &file, 1));
    TEST_ASSERT_FALSE(uartview_send_adjust(&c, &send, -1));
    /* the count: 1 to 32, clamped */
    TEST_ASSERT_FALSE(uartview_send_adjust(&c, &count, -1));
    TEST_ASSERT_EQUAL_UINT8(1, c.count);
    for (int i = 2; i <= 32; i++) {
        TEST_ASSERT_TRUE(uartview_send_adjust(&c, &count, 1));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, c.count);
    }
    TEST_ASSERT_FALSE(uartview_send_adjust(&c, &count, 1));
    TEST_ASSERT_EQUAL_UINT8(32, c.count);
    TEST_ASSERT_TRUE(uartview_send_adjust(&c, &count, -1));
    TEST_ASSERT_EQUAL_UINT8(31, c.count);
    /* the nibble toggles either way */
    TEST_ASSERT_TRUE(uartview_send_adjust(&c, &nibble, 1));
    TEST_ASSERT_TRUE(c.low_nibble);
    TEST_ASSERT_TRUE(uartview_send_adjust(&c, &nibble, -1));
    TEST_ASSERT_FALSE(c.low_nibble);
}

/* Every byte is reachable from every other with the nibble steps, and the other nibble never moves.
 */
static void test_a_nibble_wraps_inside_itself(void)
{
    uartapp_send_cfg_t c;
    uartview_send_cfg_reset(&c);
    uartrow_t byte = {UARTROW_BYTE, 3};
    for (unsigned start = 0; start < 256; start++) {
        for (int low = 0; low <= 1; low++) {
            for (int dir = -1; dir <= 1; dir += 2) {
                c.data[3] = (uint8_t)start;
                c.low_nibble = low != 0;
                TEST_ASSERT_TRUE(uartview_send_adjust(&c, &byte, dir));
                unsigned was = start;
                unsigned now = c.data[3];
                unsigned up = dir > 0 ? 1u : 15u; /* a step of one, mod 16 */
                if (low != 0) {
                    TEST_ASSERT_EQUAL_UINT(was & 0xF0u, now & 0xF0u);
                    TEST_ASSERT_EQUAL_UINT((was + up) & 0x0Fu, now & 0x0Fu);
                } else {
                    TEST_ASSERT_EQUAL_UINT(was & 0x0Fu, now & 0x0Fu);
                    TEST_ASSERT_EQUAL_UINT(((was >> 4) + up) & 0x0Fu, now >> 4);
                }
            }
        }
    }
    /* 0x00 to 0xFF by sixteen steps of each nibble */
    c.data[3] = 0;
    c.low_nibble = true;
    for (int i = 0; i < 15; i++) {
        (void)uartview_send_adjust(&c, &byte, 1);
    }
    c.low_nibble = false;
    for (int i = 0; i < 15; i++) {
        (void)uartview_send_adjust(&c, &byte, 1);
    }
    TEST_ASSERT_EQUAL_UINT8(0xFF, c.data[3]);
    uartrow_t far = {UARTROW_BYTE, 32};
    TEST_ASSERT_FALSE(uartview_send_adjust(&c, &far, 1));
    TEST_ASSERT_FALSE(uartview_send_adjust(&c, &byte, 0));
}

static void test_the_tx_confirmation_names_the_pin_and_the_risk(void)
{
    char msg[160];
    uartview_tx_message("LAB_UART_TX", msg, sizeof msg);
    TEST_ASSERT_NOT_NULL(strstr(msg, "LAB_UART_TX"));
    TEST_ASSERT_NOT_NULL(strstr(msg, "Own hardware only"));
    TEST_ASSERT_NOT_NULL(strstr(msg, "3.3 V"));
    TEST_ASSERT_NOT_NULL(strstr(msg, "may be damaged"));
    TEST_ASSERT_TRUE(strlen(msg) <= 159u); /* the dialog's message limit */
    uartview_tx_message(NULL, msg, sizeof msg);
    TEST_ASSERT_NOT_NULL(strstr(msg, "TX is driven"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_home_list);
    RUN_TEST(test_a_list_step_stops_at_its_ends);
    RUN_TEST(test_counts_are_compact);
    RUN_TEST(test_the_monitor_header);
    RUN_TEST(test_rows_are_aligned_to_the_newest_byte);
    RUN_TEST(test_the_window_and_the_scroll);
    RUN_TEST(test_row_text);
    RUN_TEST(test_the_status_line);
    RUN_TEST(test_failure_texts);
    RUN_TEST(test_the_canned_sends);
    RUN_TEST(test_the_send_rows);
    RUN_TEST(test_the_send_labels_and_values);
    RUN_TEST(test_the_editor_adjusts_only_what_it_should);
    RUN_TEST(test_a_nibble_wraps_inside_itself);
    RUN_TEST(test_the_tx_confirmation_names_the_pin_and_the_risk);
    return UNITY_END();
}
