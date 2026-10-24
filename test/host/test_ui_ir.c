/*
 * test_ui_ir.c — the real IR app on the host UI test port (TASKS.md
 * STARK-0305) with a fake stark_ir and a settable stark_storage: RX follows
 * the app, captures of every protocol are listed newest first with their
 * carrier and length, at most six are kept, and every save refusal is
 * surfaced. The frame with captures is pinned after inspection.
 *
 * STARK-0306: with the worker run inline and an in-memory captures/
 * directory, Browse lists *.ir files (saved captures first), marks a file
 * that does not parse as invalid and refuses it, shows the exact
 * transmission before sending, sends only after OK, surfaces a refused or
 * busy transmitter, and BACK returns to the receive view without leaving
 * the app (which still stops the receiver when it does leave).
 */
#include <stdio.h>
#include <string.h>
#include "ir_codec.h"
#include "ir_raw.h"
#include "irapp.h"
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_ui.h"
#include "ui_port.h"
#include "ui_port_ir.h"
#include "ui_port_storage.h"
#include "unity.h"

extern const stark_app_t app_ir;
static const stark_app_t *const k_apps[] = {&app_ir};

void setUp(void)
{}
void tearDown(void)
{}

static void frame(void)
{
    stark_ui_tick();
}

static void capture(ir_protocol_t p, uint16_t a, uint16_t c)
{
    ir_signal_t s = {.frame = {.protocol = p, .address = a, .command = c}};
    (void)ir_encode(&s.frame, &s.timings, &s.carrier_hz);
    ui_port_ir_receive(&s);
    frame();
}

static void test_ir_app(void)
{
    hal_host_set_now_us(0);
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, 1));
    TEST_ASSERT_FALSE(ui_port_ir_rx_on());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("ir"));
    frame();
    TEST_ASSERT_TRUE(ui_port_ir_rx_on()); /* receive-first: on while the app is open */

    /* Save last with nothing captured */
    ui_port_short(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: save refused: nothing captured"));

    capture(IR_PROTO_NEC, 0x00, 0x12);
    capture(IR_PROTO_RC5, 0x05, 0x35);
    capture(IR_PROTO_SIRC20, 0x1A5, 0x33);
    ir_signal_t raw = {.carrier_hz = 38000, .timings = {.n = 5, .us = {3000, 700, 900, 700, 900}}};
    ui_port_ir_receive(&raw);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: view NEC 00/12"));
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: view RAW 5"));
    uint64_t h = ui_port_fb_hash();
    if (h != UINT64_C(0x1cfe8778cd217edd)) {
        (void)ui_port_dump_ppm("ui_ir.ppm");
        printf("ir frame hash 0x%016llx (see ui_ir.ppm)\n", (unsigned long long)h);
    }
    TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x1cfe8778cd217edd), h);

    /* at most six kept: three more push the oldest out */
    capture(IR_PROTO_RC6, 0x10, 0x20);
    capture(IR_PROTO_NECX, 0x1234, 0x56);
    capture(IR_PROTO_SIRC12, 0x01, 0x15);
    capture(IR_PROTO_SIRC15, 0x42, 0x2A);

    /* saving: no card, then a card (the UI port runs no worker) */
    ui_port_reset_records();
    ui_port_storage_set(STARK_STORAGE_ABSENT, 0, 0);
    frame();
    ui_port_short(STARK_KEY_OK); /* Save last */
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: save refused: no card"));
    ui_port_storage_set(STARK_STORAGE_MOUNTED, 1u << 20, 1u << 20);
    frame();
    ui_port_short(STARK_KEY_DOWN); /* Browse saved: no worker on this port */
    frame();
    ui_port_short(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: browse: Not supported"));
    ui_port_short(STARK_KEY_BACK); /* the empty list -> the receive view */
    frame();
    TEST_ASSERT_TRUE(ui_port_ir_rx_on());
    ui_port_short(STARK_KEY_DOWN);
    frame();
    ui_port_short(STARK_KEY_DOWN); /* the newest capture */
    frame();
    ui_port_short(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: save refused: Not supported"));

    ui_port_short(STARK_KEY_BACK);
    frame();
    TEST_ASSERT_FALSE(ui_port_ir_rx_on()); /* off when the app stops */
}

static void put_signal(const char *name, ir_protocol_t p, uint16_t a, uint16_t c)
{
    static char text[IR_RAW_TEXT_MAX];
    ir_signal_t s = {.frame = {.protocol = p, .address = a, .command = c}};
    TEST_ASSERT_EQUAL(STARK_OK, ir_encode(&s.frame, &s.timings, &s.carrier_hz));
    size_t n = ir_raw_format(&s, text, sizeof text - 1);
    TEST_ASSERT_NOT_EQUAL(0, n);
    text[n] = '\0';
    ui_port_storage_add_file(name, text);
}

static void press(stark_key_t k)
{
    ui_port_short(k);
    frame();
    frame(); /* worker notifications land on the next tick */
}

static void test_ir_browse(void)
{
    ui_port_worker_inline(true);
    ui_port_storage_clear_files();
    put_signal("lab_nec.ir", IR_PROTO_NEC, 0x00, 0x12);
    ui_port_storage_add_file("lab_raw.ir", "stark-ir 1\ncarrier 38000\n"
                                           "timings 3000 700 900 700 900\n");
    ui_port_storage_add_file("lab_bad.ir", "stark-ir 1\ncarrier 38000\ntimings 9000 0 560\n");
    ui_port_storage_add_file("notes.txt", "not a signal\n");
    ui_port_storage_set(STARK_STORAGE_MOUNTED, 1u << 20, 1u << 20);
    ui_port_reset_records();
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("ir"));
    frame();
    TEST_ASSERT_TRUE(ui_port_ir_rx_on());

    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK);                                             /* Browse saved */
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: browse 3 files")); /* notes.txt skipped */
    size_t n = 0;
    const irbrowse_entry_t *e = irbrowse_entries(&n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_STRING("lab_bad.ir", e[0].name);
    TEST_ASSERT_FALSE(e[0].valid);
    TEST_ASSERT_EQUAL_STRING("(invalid)", e[0].what);
    TEST_ASSERT_EQUAL_STRING("lab_nec.ir", e[1].name);
    TEST_ASSERT_EQUAL_STRING("NEC 00/12", e[1].what);
    TEST_ASSERT_EQUAL_STRING("RAW 5", e[2].what);
    uint64_t h = ui_port_fb_hash();
    if (h != UINT64_C(0x5504090910b364ba)) {
        (void)ui_port_dump_ppm("ui_ir_browse.ppm");
        printf("ir browse hash 0x%016llx (see ui_ir_browse.ppm)\n", (unsigned long long)h);
    }
    TEST_ASSERT_EQUAL_HEX64(UINT64_C(0x5504090910b364ba), h);

    irbrowse_activate(0); /* not selectable on screen; refused anyway */
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: send refused: invalid file"));

    /* the selection starts on the first valid file: lab_nec.ir */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains(
        "irapp: confirm lab_nec.ir: NEC 00/12, 38000 Hz, 67 pulses, 68 ms. Own devices only."));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Transmit IR?\""));
    press(STARK_KEY_BACK);
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: send cancelled"));
    TEST_ASSERT_EQUAL(0, ui_port_ir_tx_count()); /* nothing without OK */

    press(STARK_KEY_OK);
    press(STARK_KEY_OK); /* confirm */
    TEST_ASSERT_EQUAL(1, ui_port_ir_tx_count());
    TEST_ASSERT_EQUAL(IR_PROTO_NEC, ui_port_ir_last_tx()->frame.protocol);
    TEST_ASSERT_EQUAL_HEX16(0x12, ui_port_ir_last_tx()->frame.command);
    TEST_ASSERT_EQUAL(38000, ui_port_ir_last_tx()->carrier_hz);
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: sent lab_nec.ir"));

    ui_port_ir_fail_next_tx(STARK_ERR_BUSY); /* inside the 1 s interval */
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: send refused: busy"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Wait a moment\""));
    press(STARK_KEY_OK); /* dismiss */
    ui_port_ir_fail_next_tx(STARK_ERR_INVALID_ARG);
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: send refused: Invalid argument"));
    TEST_ASSERT_EQUAL(1, ui_port_ir_tx_count());

    /* the file vanishes between listing and loading */
    ui_port_storage_clear_files();
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: send refused: cannot read lab_nec.ir"));

    /* BACK: the receive view, not the launcher; save a capture; it lists first */
    press(STARK_KEY_BACK);
    TEST_ASSERT_TRUE(ui_port_ir_rx_on());
    put_signal("lab_nec.ir", IR_PROTO_NEC, 0x00, 0x12);
    capture(IR_PROTO_RC5, 0x05, 0x35);
    press(STARK_KEY_OK); /* Save last */
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: saved ir_0001.ir"));
    ir_signal_t back;
    const char *text = ui_port_storage_file("ir_0001.ir");
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_EQUAL(STARK_OK, ir_raw_parse(text, strlen(text), &back));
    TEST_ASSERT_EQUAL(IR_PROTO_RC5, back.frame.protocol);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: browse 2 files"));
    e = irbrowse_entries(&n);
    TEST_ASSERT_EQUAL_STRING("ir_0001.ir", e[0].name);
    TEST_ASSERT_EQUAL_STRING("RC5 05/35", e[0].what);

    /* the card goes while the list is open: it empties */
    ui_port_storage_set(STARK_STORAGE_ABSENT, 0, 0);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: browse: card gone"));
    (void)irbrowse_entries(&n);
    TEST_ASSERT_EQUAL(0, n);
    press(STARK_KEY_OK); /* nothing to send */
    TEST_ASSERT_EQUAL(1, ui_port_ir_tx_count());

    /* no card: an empty list, said so */
    press(STARK_KEY_BACK);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: browse: no card"));
    (void)irbrowse_entries(&n);
    TEST_ASSERT_EQUAL(0, n);

    press(STARK_KEY_BACK); /* list -> view */
    TEST_ASSERT_TRUE(ui_port_ir_rx_on());
    press(STARK_KEY_BACK); /* view -> launcher: the app stops after a mode switch */
    TEST_ASSERT_FALSE(ui_port_ir_rx_on());
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop ir"));
    ui_port_worker_inline(false);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ir_app);
    RUN_TEST(test_ir_browse);
    return UNITY_END();
}
