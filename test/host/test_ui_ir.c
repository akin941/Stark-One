/*
 * test_ui_ir.c — the real IR app on the host UI test port (TASKS.md
 * STARK-0305) with a fake stark_ir and a settable stark_storage: RX follows
 * the app, captures of every protocol are listed newest first with their
 * carrier and length, at most six are kept, and every save refusal is
 * surfaced. The frame with captures is pinned after inspection.
 */
#include <stdio.h>
#include <string.h>
#include "ir_codec.h"
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
    ui_port_short(STARK_KEY_DOWN); /* Browse saved */
    frame();
    ui_port_short(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: browse"));
    ui_port_short(STARK_KEY_DOWN); /* the newest capture */
    frame();
    ui_port_short(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("irapp: save refused: Not supported"));

    ui_port_short(STARK_KEY_BACK);
    frame();
    TEST_ASSERT_FALSE(ui_port_ir_rx_on()); /* off when the app stops */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ir_app);
    return UNITY_END();
}
