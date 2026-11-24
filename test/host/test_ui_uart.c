/*
 * test_ui_uart.c — the real UART app on the host UI port (TASKS.md STARK-0705) over the real
 * session (components/stark_lab/lab_uart_svc.c), the real pin hold, the production transport guard
 * and the test-only peer: the monitor in hex and ASCII with its scrolling, counted errors and a
 * peer at the wrong baud, the card log (no card, a log of the scene whose CRC-32 is the
 * generator's, a card that goes away), Send behind its confirmation (BACK touches nothing, OK arms
 * the rest of the view, a byte editor, the file, a failing write), the loopback view and its
 * result, auto-baud applied to the Baud row, the pins held only while a view runs and free on
 * every exit, and a monitor whose driver fails. Bytes are the model's, never a line on a board
 * (ROADMAP V1 exit 13). Frames pinned after inspection.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_lab.h"
#include "fake_lab_uart_vectors.h"
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_input.h"
#include "stark_lab.h"
#include "stark_ui.h"
#include "storage_fake.h"
#include "ui_port.h"
#include "ui_port_uart.h"
#include "unity.h"

extern const stark_app_t app_uart;
static const stark_app_t *const k_apps[] = {&app_uart};
static bool s_ui_ready;

/* The home rows, and the rows of the other views, as the app lists them. */
enum { HOME_MONITOR, HOME_SEND, HOME_LOOP, HOME_AUTOBAUD, HOME_LOG, HOME_BAUD, HOME_FORMAT };
enum { SEND_FILE = 5, SEND_BYTES = 6, SEND_NIBBLE = 7, SEND_BYTE0 = 8 };

void setUp(void)
{
    hal_host_set_now_us(0);
    ui_port_uart_reset();
    sfake_reset();
    ui_port_worker_inline(true);
    ui_port_worker_stop_after(0);
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

/* A key and two frames: the second takes what the first one set off (a worker that ran inside the
 * key's handling announces itself with an event, which is dispatched on the next tick). */
static void press(stark_key_t k)
{
    ui_port_short(k);
    frame();
    frame();
}

static void presses(stark_key_t k, int n)
{
    for (int i = 0; i < n; i++) {
        press(k);
    }
}

/* A frame pinned after inspection; PIN_COLLECT=1 prints every hash instead of asserting. */
static void pin(const char *name, uint64_t want)
{
    uint64_t h = ui_port_fb_hash();
    bool collect = getenv("PIN_COLLECT") != NULL;
    if (h != want || collect) {
        char path[64];
        snprintf(path, sizeof path, "ui_uart_%s.ppm", name);
        (void)ui_port_dump_ppm(path);
        printf("uart %s hash 0x%016llx (see %s)\n", name, (unsigned long long)h, path);
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
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("uart"));
    frame();
}

static void home_row(int row)
{
    /* the selection moves with wrap, so go down from wherever it is: rows are only ever
     * visited from the top in these tests (the app was just started or came back to the top) */
    presses(STARK_KEY_DOWN, row);
}

/* Monitor with the worker ending after `polls` waits of 10 ms. */
static void open_monitor(unsigned polls)
{
    ui_port_worker_stop_after(polls);
    press(STARK_KEY_OK);
    frame(); /* the worker's note reaches the screen */
}

static void assert_nothing_held_nothing_installed(void)
{
    TEST_ASSERT_FALSE(ui_port_uart_held(38));
    TEST_ASSERT_FALSE(ui_port_uart_held(39));
    TEST_ASSERT_EQUAL_UINT(ui_port_uart_opens(), ui_port_uart_closes());
    TEST_ASSERT_FALSE(fake_lab_uart_is_open());
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(39));
}

/* ---- home and the settings --------------------------------------------------------------------
 */

static void test_home_lists_the_tools_and_holds_nothing(void)
{
    start();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: start uart"));
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: home"));
    assert_nothing_held_nothing_installed();
    TEST_ASSERT_EQUAL_UINT(0, ui_port_uart_opens());
    pin("home", UINT64_C(0x4681fdf319ac775b));
    press(STARK_KEY_BACK);
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop uart"));
    assert_nothing_held_nothing_installed();
}

static void test_baud_and_format_step_along_their_lists_and_stop_at_the_ends(void)
{
    start();
    home_row(HOME_BAUD);
    press(STARK_KEY_RIGHT);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: 230400 8N1"));
    presses(STARK_KEY_RIGHT, 3); /* 460 800, 921 600, then the end */
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: 921600 8N1"));
    int rejects = ui_port_buzzer_rejects();
    press(STARK_KEY_RIGHT);
    TEST_ASSERT_EQUAL_INT(rejects + 1, ui_port_buzzer_rejects());
    pin("home_921600", UINT64_C(0xd98b869c332e1fdf));
    presses(STARK_KEY_LEFT, 10);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: 1200 8N1"));
    press(STARK_KEY_DOWN); /* Format */
    presses(STARK_KEY_RIGHT, 4);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: 1200 7E1"));
    pin("home_1200_7e1", UINT64_C(0x39fe49e7a53dbf37));
    presses(STARK_KEY_UP, 1);
    press(STARK_KEY_OK); /* nothing happens on a setting row: the baud */
    TEST_ASSERT_FALSE(ui_port_log_contains("lab: uart open"));
    assert_nothing_held_nothing_installed(); /* a setting at home drives and holds nothing */
}

/* ---- the monitor -------------------------------------------------------------------------------
 */

static void test_the_monitor_shows_the_peers_stream_in_hex_then_ascii(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 115200, 0);
    start();
    open_monitor(40);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart open 115200 8N1"));
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: monitor 115200 8N1"));
    TEST_ASSERT_FALSE(fake_lab_uart_tx());   /* a monitor drives nothing */
    TEST_ASSERT_TRUE(ui_port_uart_held(38)); /* but owns both pins while it runs */
    TEST_ASSERT_TRUE(ui_port_uart_held(39));
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, ui_port_uart_take(38)); /* the GPIO app cannot drive them */
    stark_lab_uart_stats_t st;
    stark_lab_uart_stats(&st);
    TEST_ASSERT_EQUAL_UINT32(1300, st.rx);
    TEST_ASSERT_EQUAL_UINT(1300, st.buffered);
    TEST_ASSERT_EQUAL_UINT32(0, st.framing + st.parity + st.overrun + st.lost);
    TEST_ASSERT_TRUE(ui_port_uart_data_events() >= 1u);
    frame();
    pin("monitor_hex", UINT64_C(0xced81d3f71c3e023));
    press(STARK_KEY_OK); /* ASCII */
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: view ascii"));
    pin("monitor_ascii", UINT64_C(0x9a21c5b5a6b87c63));
    press(STARK_KEY_OK);      /* hex again */
    presses(STARK_KEY_UP, 3); /* three rows back in time */
    pin("monitor_hex_back_3", UINT64_C(0x7f2abefbd4d68066));
    presses(STARK_KEY_DOWN, 1);
    press(STARK_KEY_LEFT); /* a page back */
    pin("monitor_hex_page_back", UINT64_C(0x7741f31e1f2a6b41));
    press(STARK_KEY_RIGHT);
    press(STARK_KEY_RIGHT); /* and forward again, to the newest */
    pin("monitor_hex_newest", UINT64_C(0xced81d3f71c3e023));
    press(STARK_KEY_BACK); /* back to home */
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: monitor closed"));
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart closed"));
    assert_nothing_held_nothing_installed();
    pin("home_after_monitor", UINT64_C(0x4681fdf319ac775b));
    press(STARK_KEY_BACK);
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop uart"));
}

static void test_the_monitor_counts_and_shows_errors(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 9600, 0); /* the tool is at 115 200 */
    start();
    open_monitor(40);
    stark_lab_uart_stats_t st;
    stark_lab_uart_stats(&st);
    TEST_ASSERT_TRUE(st.framing > 0u);
    TEST_ASSERT_TRUE(ui_port_uart_error_events() >= 1u);
    frame();
    pin("monitor_errors", UINT64_C(0xc8009bac7784786f));
    press(STARK_KEY_BACK);
    assert_nothing_held_nothing_installed();
}

static void test_a_monitor_after_a_baud_change_uses_it(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_BINARY, 9600, 0);
    start();
    home_row(HOME_BAUD);
    presses(STARK_KEY_LEFT, 4); /* 9 600 */
    presses(STARK_KEY_UP, 5);   /* Monitor */
    open_monitor(40);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart open 9600 8N1"));
    stark_lab_uart_stats_t st;
    stark_lab_uart_stats(&st);
    TEST_ASSERT_EQUAL_UINT32(0, st.framing);
    TEST_ASSERT_TRUE(st.rx > 300u && st.rx <= 400u); /* 400 ms of 9 600 baud: about 384 bytes */
    frame();
    pin("monitor_9600_binary", UINT64_C(0xabaddc1289db9b1e));
    press(STARK_KEY_BACK);
}

static void test_a_monitor_refuses_when_the_pins_are_not_free(void)
{
    start();
    TEST_ASSERT_EQUAL(STARK_OK, ui_port_uart_take(38)); /* the GPIO app has the TX pin */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: not opened: Busy"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"UART\""));
    pin("monitor_busy", UINT64_C(0x95669efbaec465b4));
    press(STARK_KEY_OK);                      /* the alert */
    TEST_ASSERT_FALSE(ui_port_uart_held(39)); /* nothing half taken */
    TEST_ASSERT_TRUE(ui_port_uart_held(38));  /* the GPIO app keeps its own */
    TEST_ASSERT_EQUAL_UINT(0, ui_port_uart_opens());
    ui_port_uart_give(38);
    ui_port_worker_stop_after(5);
    press(STARK_KEY_OK); /* and it works once the pin is free */
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart open 115200 8N1"));
    press(STARK_KEY_BACK);
    assert_nothing_held_nothing_installed();
}

static void test_a_failing_driver_ends_the_monitor_with_an_alert_and_frees_the_pins(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 115200, 0);
    start();
    ui_port_uart_fail(2, 3); /* the fourth read fails */
    open_monitor(40);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart failed: I/O error"));
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: monitor failed"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"UART\""));
    frame();
    pin("monitor_failed", UINT64_C(0xe39cc5171dcb98e8));
    assert_nothing_held_nothing_installed();
    press(STARK_KEY_OK);   /* the alert */
    press(STARK_KEY_BACK); /* out of the app: it was home all along */
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop uart"));
}

static void test_leaving_the_app_from_the_monitor_closes_everything(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 115200, 0);
    start();
    open_monitor(10);
    TEST_ASSERT_TRUE(ui_port_uart_held(38));
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_pop_to_root()); /* a long BACK from anywhere */
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop uart"));
    assert_nothing_held_nothing_installed();
}

/* ---- the log -----------------------------------------------------------------------------------
 */

static void test_the_log_needs_a_card_and_says_so(void)
{
    sfake_set_state(STARK_STORAGE_ABSENT);
    start();
    home_row(HOME_LOG);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: log refused: no card"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Log\""));
    pin("log_no_card", UINT64_C(0xf5ce4af9464eb248));
    press(STARK_KEY_OK);
    TEST_ASSERT_FALSE(ui_port_log_contains("uartapp: log on"));
    pin("home_log_off", UINT64_C(0x2423e8fb71354e3b));
}

static void test_a_logged_monitor_writes_the_scene_whose_crc_the_generator_knows(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 115200, 0);
    start();
    home_row(HOME_LOG);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: log on"));
    pin("home_log_on", UINT64_C(0x25eeea68b75abf61));
    presses(STARK_KEY_UP, 4); /* Monitor */
    open_monitor(40);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: log started uart_0001.log"));
    char want[96];
    snprintf(want, sizeof want, "uartapp: log closed uart_0001.log 1300 bytes crc 0x%08x",
             (unsigned)FAKE_LAB_UART_TEXT_SCENE_CRC);
    TEST_ASSERT_TRUE(ui_port_log_contains(want));
    size_t len = 0;
    const uint8_t *file = sfake_get(STARK_DIR_LOGS, "uart_0001.log", &len);
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_UINT(1300, len);
    TEST_ASSERT_EQUAL_HEX32(FAKE_LAB_UART_TEXT_SCENE_CRC, stark_lab_uart_crc32(0, file, len));
    for (size_t j = 0; j < len; j++) {
        TEST_ASSERT_EQUAL_UINT8(fake_lab_uart_scene_byte(FAKE_LAB_UART_TEXT, j), file[j]);
    }
    TEST_ASSERT_EQUAL_UINT(0, sfake_open_handles()); /* the card can be ejected */
    frame();
    pin("monitor_logged", UINT64_C(0xced81d3f71c3e023));
    press(STARK_KEY_BACK);
    /* the console log carries names and counts, never the received bytes */
    TEST_ASSERT_FALSE(ui_port_log_contains("stark uart scene"));
    /* a second run is the next file */
    open_monitor(40);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: log started uart_0002.log"));
    press(STARK_KEY_BACK);
    TEST_ASSERT_EQUAL_UINT(2, sfake_files(STARK_DIR_LOGS));
}

static void test_a_card_that_goes_away_mid_log_stops_the_log_and_the_monitor_goes_on(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_BINARY, 115200, 0);
    start();
    home_row(HOME_LOG);
    press(STARK_KEY_OK);
    presses(STARK_KEY_UP, 4);
    sfake_fail(SFAKE_WRITE, 0); /* the first flush, at 2 kB or at the end, fails */
    open_monitor(40);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: log failed:"));
    stark_lab_uart_stats_t st;
    stark_lab_uart_stats(&st);
    TEST_ASSERT_EQUAL_UINT32(1024, st.rx); /* the monitor kept reading */
    frame();
    pin("monitor_log_stopped", UINT64_C(0x45bbbf21377d6aa4));
    press(STARK_KEY_BACK);
    assert_nothing_held_nothing_installed();
}

/* ---- send
 * ---------------------------------------------------------------------------------------- */

static void open_send(void)
{
    home_row(HOME_SEND);
    press(STARK_KEY_OK);
}

static void test_send_asks_before_the_first_drive_and_back_touches_nothing(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_ECHO, 115200, 0);
    start();
    open_send();
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: send view"));
    TEST_ASSERT_TRUE(ui_port_uart_held(38));
    TEST_ASSERT_TRUE(ui_port_uart_held(39));
    TEST_ASSERT_FALSE(fake_lab_uart_tx()); /* TX is off until confirmed */
    pin("send_view", UINT64_C(0xbbf2ab6f393859b3));
    press(STARK_KEY_OK); /* the first canned send: \r\n */
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Drive LAB_UART_TX?\""));
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: confirm tx"));
    pin("send_confirm", UINT64_C(0x8ff6297174e7abae));
    press(STARK_KEY_BACK);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: tx cancelled"));
    TEST_ASSERT_FALSE(ui_port_log_contains("lab: uart send"));
    TEST_ASSERT_FALSE(fake_lab_uart_tx());
    press(STARK_KEY_OK); /* asks again */
    TEST_ASSERT_EQUAL_UINT(2, count_logs("dialog: open \"Drive LAB_UART_TX?\""));
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart tx on"));
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart send 2 bytes"));
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: sent 2 bytes"));
    TEST_ASSERT_TRUE(fake_lab_uart_tx());
    pin("send_sent", UINT64_C(0xe76f6682505201a5));
    /* the rest of the view is armed: another send asks nothing */
    press(STARK_KEY_DOWN); /* AT\r\n */
    press(STARK_KEY_OK);
    TEST_ASSERT_EQUAL_UINT(2, count_logs("dialog: open \"Drive LAB_UART_TX?\""));
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart send 4 bytes"));
    TEST_ASSERT_EQUAL_UINT(2, count_logs("lab: uart send"));
    /* leaving drops the arm with the pins: the next visit asks again */
    press(STARK_KEY_BACK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart closed"));
    assert_nothing_held_nothing_installed();
    open_send();
    press(STARK_KEY_OK);
    TEST_ASSERT_EQUAL_UINT(3, count_logs("dialog: open \"Drive LAB_UART_TX?\""));
    press(STARK_KEY_BACK);
    press(STARK_KEY_BACK);
    assert_nothing_held_nothing_installed();
}

static void test_the_byte_editor_sends_exactly_the_bytes_it_shows(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_LOOP, 0, 0);
    start();
    open_send();
    presses(STARK_KEY_DOWN, SEND_BYTES);
    press(STARK_KEY_RIGHT); /* two bytes */
    pin("send_two_bytes", UINT64_C(0x939f5cdbb0e7364d));
    press(STARK_KEY_DOWN);  /* Nibble */
    press(STARK_KEY_RIGHT); /* the low one */
    press(STARK_KEY_DOWN);  /* Byte 0 */
    presses(STARK_KEY_RIGHT, 5);
    press(STARK_KEY_DOWN); /* Byte 1 */
    press(STARK_KEY_RIGHT);
    pin("send_edited", UINT64_C(0x3882c0af82343e8d));
    press(STARK_KEY_DOWN); /* Send bytes */
    press(STARK_KEY_OK);   /* the confirmation */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart send 2 bytes"));
    /* the loopback gives them back: 05 01 */
    ui_port_uart_advance_us(100000);
    uint8_t got[4];
    size_t n = 0;
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_uart_poll(got, sizeof got, 0, &n));
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_EQUAL_UINT8(0x05, got[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, got[1]);
    pin("send_edited_sent", UINT64_C(0x27ab41dabbac4203));
    press(STARK_KEY_BACK);
    press(STARK_KEY_BACK);
}

static void test_the_file_is_read_strictly_and_sent_once(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_LOOP, 0, 0);
    start();
    open_send();
    presses(STARK_KEY_DOWN, SEND_FILE);
    press(STARK_KEY_OK); /* the confirmation */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart tx on"));
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: file no file"));
    pin("send_file_missing", UINT64_C(0x631eb3cc8492c543));
    const char *hex = "hex:41 54 0d 0a";
    sfake_put(STARK_DIR_APPS, "uart_send.txt", (const uint8_t *)hex, strlen(hex));
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: file sent 4 bytes"));
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart send 4 bytes"));
    pin("send_file_sent", UINT64_C(0xee7e99f7fba2f653));
    const char *bad = "this line is far too long to be sent in one go: 40+";
    sfake_put(STARK_DIR_APPS, "uart_send.txt", (const uint8_t *)bad, strlen(bad));
    unsigned sends = count_logs("lab: uart send");
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: file rejected: too long"));
    TEST_ASSERT_EQUAL_UINT(sends, count_logs("lab: uart send")); /* rejected whole: nothing sent */
    pin("send_file_rejected", UINT64_C(0xfc91986f62b043f1));
    const uint8_t binary[] = {'a', 0x01, 'b'};
    sfake_put(STARK_DIR_APPS, "uart_send.txt", binary, sizeof binary);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: file rejected: bad bytes"));
    TEST_ASSERT_EQUAL_UINT(sends, count_logs("lab: uart send"));
    sfake_fail(SFAKE_READ, 0);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: file file error"));
    press(STARK_KEY_BACK);
    press(STARK_KEY_BACK);
    assert_nothing_held_nothing_installed();
}

static void test_a_failing_write_alerts_and_the_pins_are_freed(void)
{
    start();
    open_send();
    press(STARK_KEY_OK);
    press(STARK_KEY_OK); /* confirmed: the first send goes */
    ui_port_uart_fail(3, 0);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: send failed:"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"UART\""));
    assert_nothing_held_nothing_installed();
    press(STARK_KEY_OK);
    pin("home_after_send_failed", UINT64_C(0x4681fdf319ac775b));
    press(STARK_KEY_BACK);
}

/* ---- the loopback -------------------------------------------------------------------------------
 */

static void open_loop(void)
{
    home_row(HOME_LOOP);
    press(STARK_KEY_OK);
}

static void test_the_loopback_view_runs_the_test_and_shows_the_result(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_LOOP, 0, 0);
    start();
    open_loop();
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: loop view"));
    pin("loop_view", UINT64_C(0x2ef7ac1137f40cf2));
    press(STARK_KEY_OK); /* Run test: the confirmation first */
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Drive LAB_UART_TX?\""));
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart loop 64/64 ok"));
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: loop 64/64 ok"));
    pin("loop_ok", UINT64_C(0xf9d84e0c7f38c0d0));
    press(STARK_KEY_OK); /* again: armed, no dialog */
    TEST_ASSERT_EQUAL_UINT(1, count_logs("dialog: open \"Drive LAB_UART_TX?\""));
    TEST_ASSERT_EQUAL_UINT(2, count_logs("lab: uart loop 64/64 ok"));
    /* a new baud reopens the driver and forgets the result */
    press(STARK_KEY_DOWN);
    press(STARK_KEY_RIGHT);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart open 230400 8N1"));
    pin("loop_new_baud", UINT64_C(0xbad25b61e68928c6));
    press(STARK_KEY_UP);
    press(STARK_KEY_OK);
    TEST_ASSERT_EQUAL_UINT(3, count_logs("lab: uart loop 64/64 ok"));
    TEST_ASSERT_EQUAL_UINT(1, count_logs("dialog: open \"Drive LAB_UART_TX?\"")); /* TX stayed on */
    press(STARK_KEY_BACK);
    assert_nothing_held_nothing_installed();
    pin("home_after_loop", UINT64_C(0x28c6292eb67714f9));
    press(STARK_KEY_BACK);
}

static void test_a_mismatched_peer_and_a_mute_one_are_reported_as_they_are(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_ECHO, 9600, 0);
    start();
    open_loop();
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("mismatch first 0"));
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: loop"));
    pin("loop_mismatch", UINT64_C(0xf469caf626edb62f));
    press(STARK_KEY_BACK);
    fake_lab_uart_peer(FAKE_LAB_UART_SILENT, 0, 0);
    open_loop();
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart loop 0/64 no data"));
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: loop No data back"));
    pin("loop_no_data", UINT64_C(0xf201cf223f2ef926));
    press(STARK_KEY_BACK);
    assert_nothing_held_nothing_installed();
}

static void test_a_failing_driver_ends_the_loopback_with_an_alert(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_LOOP, 0, 0);
    start();
    open_loop();
    press(STARK_KEY_OK);
    ui_port_uart_fail(3, 0);
    press(STARK_KEY_OK); /* the confirmation, then the test whose first write fails */
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: loop not run:"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"UART\""));
    assert_nothing_held_nothing_installed();
    press(STARK_KEY_OK);
    press(STARK_KEY_BACK);
}

static void test_a_format_change_that_fails_returns_home_with_the_pins_free(void)
{
    start();
    open_loop();
    press(STARK_KEY_DOWN);
    press(STARK_KEY_DOWN); /* Format */
    ui_port_uart_fail(0, 0);
    press(STARK_KEY_RIGHT); /* the driver is installed again for the new format: it refuses */
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: not changed:"));
    assert_nothing_held_nothing_installed();
    press(STARK_KEY_OK);
    press(STARK_KEY_BACK);
}

/* ---- auto-baud
 * ----------------------------------------------------------------------------------- */

static void test_autobaud_names_the_peers_baud_and_sets_the_row(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 38400, 0);
    start();
    home_row(HOME_AUTOBAUD);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: autobaud 38400"));
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: autobaud 38400"));
    assert_nothing_held_nothing_installed(); /* the RX pin was held for the capture only */
    pin("autobaud_38400", UINT64_C(0xe1d9887751cac679));
    presses(STARK_KEY_DOWN, 2); /* Baud: it follows */
    pin("autobaud_baud_row", UINT64_C(0xbb8aa58e77504495));
    presses(STARK_KEY_UP, 5); /* Monitor: at the new baud */
    open_monitor(10);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: uart open 38400 8N1"));
    press(STARK_KEY_BACK);
    assert_nothing_held_nothing_installed();
}

static void test_autobaud_says_unknown_and_leaves_the_baud_alone(void)
{
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 14400, 0);
    start();
    home_row(HOME_AUTOBAUD);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: autobaud unknown"));
    pin("autobaud_unknown", UINT64_C(0xf231596aa41a6355));
    fake_lab_uart_peer(FAKE_LAB_UART_SILENT, 0, 0);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: autobaud too few edges"));
    assert_nothing_held_nothing_installed();
    TEST_ASSERT_FALSE(ui_port_log_contains("uartapp: 14400"));
    /* the pin is the GPIO app's: nothing to listen on */
    TEST_ASSERT_EQUAL(STARK_OK, ui_port_uart_take(39));
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("uartapp: autobaud not run: Busy"));
    ui_port_uart_give(39);
}

/* ONLY=<part of a name> runs just the tests whose name has it (for finding a failure). */
#define RUN(fn)                                                                                    \
    do {                                                                                           \
        const char *only = getenv("ONLY");                                                         \
        if (only == NULL || strstr(#fn, only) != NULL) {                                           \
            RUN_TEST(fn);                                                                          \
        }                                                                                          \
    } while (0)

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0); /* the line of a test that hangs still appears */
    UNITY_BEGIN();
    RUN(test_home_lists_the_tools_and_holds_nothing);
    RUN(test_baud_and_format_step_along_their_lists_and_stop_at_the_ends);
    RUN(test_the_monitor_shows_the_peers_stream_in_hex_then_ascii);
    RUN(test_the_monitor_counts_and_shows_errors);
    RUN(test_a_monitor_after_a_baud_change_uses_it);
    RUN(test_a_monitor_refuses_when_the_pins_are_not_free);
    RUN(test_a_failing_driver_ends_the_monitor_with_an_alert_and_frees_the_pins);
    RUN(test_leaving_the_app_from_the_monitor_closes_everything);
    RUN(test_the_log_needs_a_card_and_says_so);
    RUN(test_a_logged_monitor_writes_the_scene_whose_crc_the_generator_knows);
    RUN(test_a_card_that_goes_away_mid_log_stops_the_log_and_the_monitor_goes_on);
    RUN(test_send_asks_before_the_first_drive_and_back_touches_nothing);
    RUN(test_the_byte_editor_sends_exactly_the_bytes_it_shows);
    RUN(test_the_file_is_read_strictly_and_sent_once);
    RUN(test_a_failing_write_alerts_and_the_pins_are_freed);
    RUN(test_the_loopback_view_runs_the_test_and_shows_the_result);
    RUN(test_a_mismatched_peer_and_a_mute_one_are_reported_as_they_are);
    RUN(test_a_failing_driver_ends_the_loopback_with_an_alert);
    RUN(test_a_format_change_that_fails_returns_home_with_the_pins_free);
    RUN(test_autobaud_names_the_peers_baud_and_sets_the_row);
    RUN(test_autobaud_says_unknown_and_leaves_the_baud_alone);
    return UNITY_END();
}
