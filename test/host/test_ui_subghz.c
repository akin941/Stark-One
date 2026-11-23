/*
 * test_ui_subghz.c — the real Sub-GHz app on the host UI port (TASKS.md
 * STARK-0602) with a fake stark_subghz: no module (nothing started), the home
 * list, the meter following the level, LEFT/RIGHT stepping the frequency by
 * 25 kHz up to the band's edges, a scan running and finished with its peak, scan
 * again, BACK from a view stopping the radio, leaving the app from a view
 * stopping it, a module lost while a view is open, and the module coming back.
 * Frames pinned after inspection.
 */
#include <stdio.h>
#include <string.h>
#include "stark_app.h"
#include "stark_caps.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_input.h"
#include "stark_ui.h"
#include "subghz_rssi.h"
#include "ui_port.h"
#include "ui_port_subghz.h"
#include "unity.h"

extern const stark_app_t app_subghz;
static const stark_app_t *const k_apps[] = {&app_subghz};

void setUp(void)
{}
void tearDown(void)
{}

static void frame(void)
{
    stark_ui_tick();
}

static void press(stark_key_t k)
{
    ui_port_short(k);
    frame();
}

static void pin(const char *name, uint64_t want)
{
    uint64_t h = ui_port_fb_hash();
    if (h != want) {
        char path[64];
        snprintf(path, sizeof path, "ui_subghz_%s.ppm", name);
        (void)ui_port_dump_ppm(path);
        printf("subghz %s hash 0x%016llx (see %s)\n", name, (unsigned long long)h, path);
    }
    TEST_ASSERT_EQUAL_HEX64(want, h);
}

static void launch(void)
{
    ui_port_reset_records();
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("subghz"));
    frame();
}

static void open_meter(void)
{
    press(STARK_KEY_OK); /* RSSI meter, the first row */
    frame();
}

static void test_no_module(void)
{
    hal_host_set_now_us(0);
    ui_port_subghz_reset();
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, 1));
    stark_app_set_caps(STARK_CAP_SUBGHZ); /* the launcher's gate; this tests the app's own views */
    launch();
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
    pin("no_module", UINT64_C(0x373ecaec5b6a4a43));
    press(STARK_KEY_OK); /* nothing selectable */
    press(STARK_KEY_DOWN);
    TEST_ASSERT_EQUAL(0, ui_port_subghz_meter_starts());
    TEST_ASSERT_EQUAL(0, ui_port_subghz_scan_starts());
    press(STARK_KEY_BACK);
    TEST_ASSERT_EQUAL(1, ui_port_subghz_stops()); /* leaving always returns the radio to idle */
}

static void test_home_and_the_meter(void)
{
    ui_port_subghz_reset();
    ui_port_subghz_set_ready(true);
    launch();
    TEST_ASSERT_FALSE(ui_port_subghz_receiving()); /* the home list is not a view */
    pin("home", UINT64_C(0x703b8aba07b7fe85));

    open_meter();
    TEST_ASSERT_TRUE(ui_port_subghz_receiving());
    TEST_ASSERT_EQUAL_UINT32(433920000u, ui_port_subghz_meter_hz());
    pin("meter_waiting", UINT64_C(0xdaa18d6c80d7da73));

    ui_port_subghz_set_level(-80); /* -40 dBm */
    frame();
    pin("meter_minus40", UINT64_C(0x3031b228f3814782));
    ui_port_subghz_set_level(-140); /* -70 dBm */
    frame();
    pin("meter_minus70", UINT64_C(0x2f5d56b0355c898a));
    ui_port_subghz_set_level(-111); /* -55.5 dBm: a half step shows */
    frame();
    pin("meter_minus55_5", UINT64_C(0x28b17c02720b7a8a));
    ui_port_subghz_set_level(-220); /* the floor: an empty bar */
    frame();
    pin("meter_floor", UINT64_C(0x23d0ef1952195282));
    ui_port_subghz_set_level(-40); /* -20 dBm: above the scale, a full bar */
    frame();
    pin("meter_full", UINT64_C(0x44ea71869dffba7a));

    /* LEFT/RIGHT step the frequency by 25 kHz and retune */
    unsigned starts = ui_port_subghz_meter_starts();
    press(STARK_KEY_RIGHT);
    TEST_ASSERT_EQUAL_UINT32(433945000u, ui_port_subghz_meter_hz());
    TEST_ASSERT_EQUAL(starts + 1, ui_port_subghz_meter_starts());
    pin("meter_433945", UINT64_C(0x3cdf05446848f60b)); /* a new frequency starts a new reading */
    press(STARK_KEY_LEFT);
    press(STARK_KEY_LEFT);
    TEST_ASSERT_EQUAL_UINT32(433895000u, ui_port_subghz_meter_hz());

    /* the edges of the band: it stops there and does not retune again */
    for (int i = 0; i < 40; i++) {
        press(STARK_KEY_RIGHT);
    }
    TEST_ASSERT_EQUAL_UINT32(434790000u, ui_port_subghz_meter_hz());
    unsigned at_top = ui_port_subghz_meter_starts();
    press(STARK_KEY_RIGHT);
    TEST_ASSERT_EQUAL(at_top, ui_port_subghz_meter_starts());
    pin("meter_top_edge", UINT64_C(0xf6a6e1d6d809561d));
    for (int i = 0; i < 75; i++) {
        press(STARK_KEY_LEFT);
    }
    TEST_ASSERT_EQUAL_UINT32(433050000u, ui_port_subghz_meter_hz());
    unsigned at_bottom = ui_port_subghz_meter_starts();
    press(STARK_KEY_LEFT);
    TEST_ASSERT_EQUAL(at_bottom, ui_port_subghz_meter_starts());
    pin("meter_bottom_edge", UINT64_C(0x8c998a4e74401a93));

    press(STARK_KEY_OK); /* nothing to activate on the meter */
    TEST_ASSERT_EQUAL(at_bottom, ui_port_subghz_meter_starts());
    press(STARK_KEY_BACK); /* the view, not the app */
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
    pin("home_after_meter", UINT64_C(0xcb7ac000300552a5));
    TEST_ASSERT_EQUAL_UINT32(433050000u, ui_port_subghz_meter_hz());

    /* the frequency chosen is kept for the next time this session */
    open_meter();
    TEST_ASSERT_EQUAL_UINT32(433050000u, ui_port_subghz_meter_hz());
    press(STARK_KEY_BACK);
    press(STARK_KEY_BACK); /* leaves the app */
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
}

static void test_the_scan(void)
{
    ui_port_subghz_reset();
    ui_port_subghz_set_ready(true);
    launch();
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK); /* Band scan */
    TEST_ASSERT_EQUAL(1, ui_port_subghz_scan_starts());
    TEST_ASSERT_TRUE(ui_port_subghz_receiving());
    pin("scan_running", UINT64_C(0xab6c9c6b592b18d2));
    press(STARK_KEY_OK); /* nothing is selectable while sweeping */
    TEST_ASSERT_EQUAL(1, ui_port_subghz_scan_starts());

    ui_port_subghz_finish_scan(433925000u, -80);
    frame();
    TEST_ASSERT_FALSE(ui_port_subghz_receiving()); /* idle once the sweep is over */
    pin("scan_done", UINT64_C(0x30216844861654d8));

    press(STARK_KEY_OK); /* Scan again is the one selectable row */
    TEST_ASSERT_EQUAL(2, ui_port_subghz_scan_starts());
    pin("scan_again", UINT64_C(0xab6c9c6b592b18d2));
    ui_port_subghz_finish_scan(434775000u, -111);
    frame();
    pin("scan_done_half_db", UINT64_C(0x0cc18c62446c6971));
    press(STARK_KEY_BACK);
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
    unsigned stops = ui_port_subghz_stops();
    press(STARK_KEY_BACK);
    TEST_ASSERT_EQUAL(stops + 1, ui_port_subghz_stops()); /* and leaving the app stops again */
}

static void test_leaving_the_app_from_a_view_returns_the_radio_to_idle(void)
{
    ui_port_subghz_reset();
    ui_port_subghz_set_ready(true);
    launch();
    open_meter();
    TEST_ASSERT_TRUE(ui_port_subghz_receiving());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_pop_to_root()); /* BACK held: home from anywhere */
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
    launch();
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_subghz_receiving());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_pop_to_root());
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
}

static void test_a_module_lost_while_a_view_is_open(void)
{
    ui_port_subghz_reset();
    ui_port_subghz_set_ready(true);
    launch();
    open_meter();
    ui_port_subghz_set_level(-80);
    frame();
    ui_port_subghz_set_ready(false);
    frame();
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
    pin("lost", UINT64_C(0xd92189a11140edfa));
    press(STARK_KEY_OK);
    press(STARK_KEY_RIGHT); /* nothing to retune */
    TEST_ASSERT_EQUAL(1, ui_port_subghz_meter_starts());

    ui_port_subghz_set_ready(true); /* and it comes back: the home list again */
    frame();
    pin("home_again", UINT64_C(0x703b8aba07b7fe85));
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
    press(STARK_KEY_BACK);
}

static void test_a_module_lost_while_the_scan_is_open(void)
{
    ui_port_subghz_reset();
    ui_port_subghz_set_ready(true);
    launch();
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK);
    ui_port_subghz_set_ready(false);
    frame();
    pin("lost_during_scan", UINT64_C(0xd92189a11140edfa));
    press(STARK_KEY_BACK);
}

static void test_a_start_refused_by_the_service_stays_home(void)
{
    ui_port_subghz_reset();
    ui_port_subghz_set_ready(true);
    launch();
    open_meter();
    /* the module goes before the UI has been told: the retune is refused, the app goes home */
    ui_port_subghz_refuse(STARK_ERR_STATE);
    press(STARK_KEY_RIGHT);
    pin("refused_retune", UINT64_C(0x4e5c6c574f065add));
    press(STARK_KEY_BACK); /* leaves the app: the home list was already showing */
    TEST_ASSERT_EQUAL(1, ui_port_subghz_meter_starts());
    launch();
    press(STARK_KEY_OK); /* the meter row: refused, nothing starts, still the home list */
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK); /* the scan row: the same */
    TEST_ASSERT_EQUAL(1, ui_port_subghz_meter_starts());
    TEST_ASSERT_EQUAL(0, ui_port_subghz_scan_starts());
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
    pin("home_refused", UINT64_C(0x878c154a6bcb86e1));
    ui_port_subghz_refuse(STARK_OK);
    press(STARK_KEY_OK); /* works again (the scan row is still selected) */
    TEST_ASSERT_EQUAL(1, ui_port_subghz_scan_starts());
    /* a sweep that cannot be repeated: Scan again is refused and the view stays */
    ui_port_subghz_finish_scan(433925000u, -80);
    frame();
    ui_port_subghz_refuse(STARK_ERR_STATE);
    press(STARK_KEY_OK);
    TEST_ASSERT_EQUAL(1, ui_port_subghz_scan_starts());
    press(STARK_KEY_BACK);
    press(STARK_KEY_BACK);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_module);
    RUN_TEST(test_home_and_the_meter);
    RUN_TEST(test_the_scan);
    RUN_TEST(test_leaving_the_app_from_a_view_returns_the_radio_to_idle);
    RUN_TEST(test_a_module_lost_while_a_view_is_open);
    RUN_TEST(test_a_module_lost_while_the_scan_is_open);
    RUN_TEST(test_a_start_refused_by_the_service_stays_home);
    return UNITY_END();
}
