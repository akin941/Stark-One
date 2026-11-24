/*
 * test_ui_spi.c — the real SPI app on the host UI port (TASKS.md STARK-0706) over the real rules
 * (components/stark_lab/lab_spi.c), the real module core with the fake 24C02 (the slot states and
 * the lease), the production transport guard and the test-only SPI devices: both slots with their
 * reason (no module, no chip select, owned by Sub-GHz), a JEDEC ID read behind a confirmation (BACK
 * touches nothing, OK makes exactly that one transaction, a second press asks again), the presets,
 * the clock, mode and byte editors, a result of 32 bytes in hex and ASCII, a driver-owned slot that
 * is refused with an alert and no traffic, a module swapped for a radio or pulled after the dialog
 * opened, a stale request, a busy slot, a failing bus, and a request that never outlives the app.
 * The lease is released after every transaction. Device answers are the model's, never a part on a
 * board (ROADMAP V1 exit 13). Frames pinned after inspection.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_lab.h"
#include "fake_module_catalog.h"
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_input.h"
#include "stark_lab.h"
#include "stark_ui.h"
#include "ui_port.h"
#include "ui_port_spi.h"
#include "unity.h"

extern const stark_app_t app_spi;
static const stark_app_t *const k_apps[] = {&app_spi};
static bool s_ui_ready;

void setUp(void)
{
    hal_host_set_now_us(0);
    ui_port_spi_reset();
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
}

static void frame(void)
{
    stark_ui_tick();
}

static void press(stark_key_t k)
{
    ui_port_short(k);
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
        snprintf(path, sizeof path, "ui_spi_%s.ppm", name);
        (void)ui_port_dump_ppm(path);
        printf("spi %s hash 0x%016llx (see %s)\n", name, (unsigned long long)h, path);
    }
    if (!collect) {
        TEST_ASSERT_EQUAL_HEX64(want, h);
    }
}

static void start(void)
{
    ui_port_reset_records();
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("spi"));
    frame();
}

static unsigned count_logs(const char *needle)
{
    unsigned n = 0;
    for (size_t i = 0; i < ui_port_log_count(); i++) {
        n += strstr(ui_port_log_line(i), needle) != NULL ? 1u : 0u;
    }
    return n;
}

/* Setup rows of the default configuration: 0 Clock, 1 Mode, 2-4 presets, 5 Bytes, 6 Nibble, 7-10
 * Byte 0-3, 11 Send. */
enum { ROW_CLOCK = 0, ROW_MODE = 1, ROW_PRESET = 2, ROW_COUNT = 5, ROW_NIBBLE = 6, ROW_SEND = 11 };

/* Opens setup on slot 0 (the module is fitted, Slot 0 is the first row of home). */
static void open_setup(void)
{
    press(STARK_KEY_OK);
}

static void goto_send(void)
{
    presses(STARK_KEY_DOWN, ROW_SEND);
}

static void test_home_with_an_empty_board_says_why_nothing_can_be_probed(void)
{
    start();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: start spi"));
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: home"));
    pin("home_empty", 0xeab41eefd0f42687ULL);
    press(STARK_KEY_OK); /* Slot 0: no module */
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: slot 0 refused: no module"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"No probe\""));
    pin("alert_no_module", 0x56e5d0b84d5be164ULL);
    press(STARK_KEY_OK); /* close the alert */
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK); /* Slot 1: no chip select */
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: slot 1 refused: no chip select"));
    press(STARK_KEY_OK);
    TEST_ASSERT_EQUAL_UINT(0, ui_port_spi_claims());
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    TEST_ASSERT_FALSE(ui_port_spi_armed());
}

static void test_a_driver_owned_slot_is_refused_with_an_alert_and_no_traffic(void)
{
    ui_port_spi_module(0, FAKE_MODULE_CC1101_MODULE);
    start();
    pin("home_radio", 0xe06bed3cd66237bbULL);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: slot 0 refused: owned by Sub-GHz"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"No probe\""));
    TEST_ASSERT_FALSE(ui_port_log_contains("spiapp: open slot"));
    TEST_ASSERT_EQUAL_UINT(0, ui_port_spi_claims());
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    ui_port_reset_records();
    press(STARK_KEY_OK);
    press(STARK_KEY_BACK); /* leave the app */
    ui_port_spi_module(0, FAKE_MODULE_NFC_CARRIER);
    start();
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: slot 0 refused: owned by NFC"));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
}

static void test_slot_1_never_opens_even_with_a_breakout_in_it(void)
{
    ui_port_spi_module(1, FAKE_MODULE_SPI_BREAKOUT);
    start();
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: slot 1 refused: no chip select"));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
}

static void test_a_jedec_read_asks_first_and_one_confirmation_buys_one_transaction(void)
{
    ui_port_spi_module(0, FAKE_MODULE_SPI_BREAKOUT);
    fake_lab_spi_device(FAKE_LAB_SPI_FLASH);
    start();
    pin("home_breakout", 0x8ace5912b84c71c9ULL);
    open_setup();
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: open slot 0"));
    pin("setup", 0x77d8d39a668661b5ULL);
    goto_send();
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Send SPI?\""));
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: confirm slot 0 1 MHz mode 0"));
    TEST_ASSERT_TRUE(ui_port_spi_armed());
    TEST_ASSERT_EQUAL_UINT(0, ui_port_spi_claims()); /* asking claims nothing */
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    pin("confirm", 0xe867416a5bc0ee84ULL);
    press(STARK_KEY_BACK); /* cancel */
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: cancel"));
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: cancelled slot 0"));
    TEST_ASSERT_FALSE(ui_port_spi_armed());
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL_UINT(0, ui_port_spi_claims());

    press(STARK_KEY_OK); /* Send again */
    press(STARK_KEY_OK); /* confirm */
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: ok"));
    TEST_ASSERT_TRUE(
        ui_port_log_contains("lab: spi slot 0 1 MHz mode 0 tx 9f 00 00 00 rx 00 ef 40 18"));
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: sent slot 0: rx 00 ef 40 18"));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL_UINT(1, ui_port_spi_dones());
    TEST_ASSERT_EQUAL_UINT(1, ui_port_spi_claims());
    TEST_ASSERT_EQUAL_UINT(1, ui_port_spi_releases());
    TEST_ASSERT_FALSE(ui_port_spi_armed());
    pin("result_jedec", 0xd5c1a7ef0b5b7837ULL);

    press(STARK_KEY_UP); /* the answer is in view; back to Send */
    press(STARK_KEY_OK); /* a second press asks again */
    TEST_ASSERT_EQUAL_UINT(3,
                           count_logs("dialog: open \"Send SPI?\"")); /* cancelled, sent, again */
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_spi_calls());
    press(STARK_KEY_BACK);
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL_UINT(1, ui_port_spi_claims());
}

static void test_the_presets_load_their_bytes_and_move_to_send(void)
{
    ui_port_spi_module(0, FAKE_MODULE_SPI_BREAKOUT);
    fake_lab_spi_device(FAKE_LAB_SPI_FLASH);
    start();
    open_setup();
    presses(STARK_KEY_DOWN, ROW_PRESET + 1); /* Read status */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: preset Read status"));
    press(STARK_KEY_OK); /* the selection moved to Send: it asks */
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Send SPI?\""));
    pin("confirm_status", 0xf0f4f7e2b3659d8aULL);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: spi slot 0 1 MHz mode 0 tx 05 00 rx 00 00"));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_spi_calls());
    pin("result_status", 0x04c6297ea7b0fb0dULL);
    /* the dummy read: four zeros, then back to JEDEC */
    presses(STARK_KEY_UP, 6); /* the answer, Send, then up to Dummy read (row 4 of 9) */
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(
        ui_port_log_contains("lab: spi slot 0 1 MHz mode 0 tx 00 00 00 00 rx 00 00 00 00"));
}

static void test_the_clock_mode_and_bytes_are_edited_with_left_and_right(void)
{
    ui_port_spi_module(0, FAKE_MODULE_SPI_ONLY);
    fake_lab_spi_device(FAKE_LAB_SPI_ECHO);
    start();
    open_setup();
    press(STARK_KEY_RIGHT); /* Clock: 2 MHz */
    press(STARK_KEY_DOWN);
    presses(STARK_KEY_RIGHT, 3); /* Mode 3 */
    presses(STARK_KEY_DOWN, ROW_COUNT - ROW_MODE);
    press(STARK_KEY_RIGHT); /* Bytes: 5 */
    press(STARK_KEY_DOWN);
    press(STARK_KEY_RIGHT); /* Nibble: high */
    presses(STARK_KEY_DOWN, 5);
    presses(STARK_KEY_RIGHT, 0xA); /* Byte 4 high nibble: 0xA0 */
    press(STARK_KEY_DOWN);         /* Send */
    pin("edited", 0x01ed20acf428b385ULL);
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(
        ui_port_log_contains("lab: spi slot 0 2 MHz mode 3 tx 9f 00 00 00 a0 rx 00 9f 00 00 00"));
    TEST_ASSERT_EQUAL_UINT8(3, fake_lab_spi_last_mode());
    TEST_ASSERT_EQUAL_UINT32(2000000u, fake_lab_spi_last_hz());
    /* editing forgets the result and any request */
    press(STARK_KEY_UP);
    press(STARK_KEY_LEFT);
    TEST_ASSERT_FALSE(ui_port_spi_armed());
}

static void test_a_result_of_32_bytes_fills_four_hex_and_four_text_rows(void)
{
    ui_port_spi_module(0, FAKE_MODULE_SPI_IRQ);
    fake_lab_spi_device(FAKE_LAB_SPI_ECHO);
    start();
    open_setup();
    presses(STARK_KEY_DOWN, ROW_COUNT);
    presses(STARK_KEY_RIGHT, 28); /* Bytes: 32 */
    presses(STARK_KEY_DOWN, 2 + 32);
    pin("count_32", 0x9ed228fde2596d9bULL);
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL_UINT(32, fake_lab_spi_last_len());
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: sent slot 0: rx 00 9f 00 00"));
    pin("result_32_top", 0xb358864753cc578dULL);
    presses(STARK_KEY_DOWN, 7); /* to the last text row: the whole answer was scrolled past */
    pin("result_32", 0x980fb5e0150a6249ULL);
}

static void test_a_module_swapped_for_a_radio_is_refused_and_the_radio_sees_nothing(void)
{
    ui_port_spi_module(0, FAKE_MODULE_SPI_BREAKOUT);
    fake_lab_spi_device(FAKE_LAB_SPI_FLASH);
    start();
    open_setup();
    goto_send();
    ui_port_spi_module(0, FAKE_MODULE_CC1101_MODULE);
    press(STARK_KEY_OK); /* the request is refused at once */
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: spi request refused: owned by Sub-GHz"));
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: request slot 0 refused: Not supported"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Refused\""));
    TEST_ASSERT_FALSE(ui_port_log_contains("dialog: open \"Send SPI?\""));
    pin("refused_radio", 0x3d28861bfbd483d8ULL);
    TEST_ASSERT_EQUAL_UINT(0, ui_port_spi_claims());
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
}

static void test_a_module_swapped_or_pulled_after_the_dialog_opened_is_refused(void)
{
    ui_port_spi_module(0, FAKE_MODULE_SPI_BREAKOUT);
    start();
    open_setup();
    goto_send();
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_spi_armed());
    ui_port_spi_module(0, FAKE_MODULE_CC1101_MODULE);
    press(STARK_KEY_OK); /* OK on a dialog that is out of date */
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: spi send refused: owned by Sub-GHz"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Send failed\""));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL_UINT(0, ui_port_spi_claims());
    press(STARK_KEY_OK); /* close the alert */
    press(STARK_KEY_BACK);
    ui_port_spi_module(0, FAKE_MODULE_SPI_BREAKOUT);
    press(STARK_KEY_OK);
    goto_send();
    press(STARK_KEY_OK);
    ui_port_spi_module(0, 0); /* pulled */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: spi send refused: no module"));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
}

static void test_a_request_that_waited_too_long_is_refused(void)
{
    ui_port_spi_module(0, FAKE_MODULE_SPI_BREAKOUT);
    start();
    open_setup();
    goto_send();
    press(STARK_KEY_OK);
    ui_port_spi_advance_ms(30001u);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: spi send refused: not confirmed"));
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: send slot 0 failed: Invalid state"));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    TEST_ASSERT_FALSE(ui_port_spi_armed());
}

static void test_a_busy_slot_is_reported_and_a_free_one_works_again(void)
{
    ui_port_spi_module(0, FAKE_MODULE_SPI_BREAKOUT);
    fake_lab_spi_device(FAKE_LAB_SPI_FLASH);
    start();
    open_setup();
    goto_send();
    ui_port_spi_hold(true); /* another driver has the slot */
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: spi slot 0 busy"));
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: send slot 0 failed: Busy"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Send failed\""));
    pin("busy", 0x53da6b2339b286e2ULL);
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    ui_port_spi_hold(false);
    press(STARK_KEY_OK); /* close the alert */
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL_UINT(ui_port_spi_claims(), ui_port_spi_releases() + 1u); /* the busy claim */
}

static void test_a_failing_bus_alerts_and_releases_the_lease(void)
{
    ui_port_spi_module(0, FAKE_MODULE_SPI_BREAKOUT);
    start();
    open_setup();
    goto_send();
    fake_lab_fault(FAKE_LAB_FAULT_SPI_ERROR, -1, 1);
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: send slot 0 failed: I/O error"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Send failed\""));
    TEST_ASSERT_EQUAL_UINT(1, ui_port_spi_claims());
    TEST_ASSERT_EQUAL_UINT(1, ui_port_spi_releases());
    TEST_ASSERT_FALSE(ui_port_spi_armed());
    pin("failed", 0xf8e3cf70d8e5bf22ULL);
}

static void test_back_steps_up_and_forgets_the_request_and_the_app_leaves_clean(void)
{
    ui_port_spi_module(0, FAKE_MODULE_SPI_BREAKOUT);
    start();
    open_setup();
    goto_send();
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_spi_armed());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_pop_to_root()); /* the app goes away under the dialog */
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop spi"));
    TEST_ASSERT_FALSE(ui_port_spi_armed());
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());

    start();
    open_setup();
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_spi_request(0, 1000000u, 0, (const uint8_t[]){0x9f}, 1,
                                                      &(stark_lab_spi_req_t){0}));
    TEST_ASSERT_TRUE(ui_port_spi_armed());
    press(STARK_KEY_BACK); /* setup -> home */
    TEST_ASSERT_FALSE(ui_port_spi_armed());
    TEST_ASSERT_TRUE(ui_port_log_contains("spiapp: home"));
    press(STARK_KEY_BACK); /* home -> the app stops */
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop spi"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_home_with_an_empty_board_says_why_nothing_can_be_probed);
    RUN_TEST(test_a_driver_owned_slot_is_refused_with_an_alert_and_no_traffic);
    RUN_TEST(test_slot_1_never_opens_even_with_a_breakout_in_it);
    RUN_TEST(test_a_jedec_read_asks_first_and_one_confirmation_buys_one_transaction);
    RUN_TEST(test_the_presets_load_their_bytes_and_move_to_send);
    RUN_TEST(test_the_clock_mode_and_bytes_are_edited_with_left_and_right);
    RUN_TEST(test_a_result_of_32_bytes_fills_four_hex_and_four_text_rows);
    RUN_TEST(test_a_module_swapped_for_a_radio_is_refused_and_the_radio_sees_nothing);
    RUN_TEST(test_a_module_swapped_or_pulled_after_the_dialog_opened_is_refused);
    RUN_TEST(test_a_request_that_waited_too_long_is_refused);
    RUN_TEST(test_a_busy_slot_is_reported_and_a_free_one_works_again);
    RUN_TEST(test_a_failing_bus_alerts_and_releases_the_lease);
    RUN_TEST(test_back_steps_up_and_forgets_the_request_and_the_app_leaves_clean);
    return UNITY_END();
}
