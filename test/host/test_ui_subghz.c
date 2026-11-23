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
#include "stark_storage.h"
#include "stark_ui.h"
#include "subghz_raw.h"
#include "subghz_rssi.h"
#include "subghzapp.h"
#include "ui_port.h"
#include "ui_port_storage.h"
#include "ui_port_subghz.h"
#include "unity.h"

extern const stark_app_t app_subghz;
static const stark_app_t *const k_apps[] = {&app_subghz};

void setUp(void)
{}
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
    pin("home", UINT64_C(0xf9704708d9b0c017));

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
    pin("home_after_meter", UINT64_C(0xa9c0a73bcf509637));
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
    pin("home_again", UINT64_C(0xf9704708d9b0c017));
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
    pin("refused_retune", UINT64_C(0x0e6bd8a80e7a566f));
    press(STARK_KEY_BACK); /* leaves the app: the home list was already showing */
    TEST_ASSERT_EQUAL(1, ui_port_subghz_meter_starts());
    launch();
    press(STARK_KEY_OK); /* the meter row: refused, nothing starts, still the home list */
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK); /* the scan row: the same */
    TEST_ASSERT_EQUAL(1, ui_port_subghz_meter_starts());
    TEST_ASSERT_EQUAL(0, ui_port_subghz_scan_starts());
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
    pin("home_refused", UINT64_C(0x84ef813e9b07e273));
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

/* ---- capture, save and the saved captures (STARK-0603) ------------------------------------ */

/* One frame of a pulse-width signal (400/1200 µs) closed by a gap. */
static const subghz_sym_t k_frame[16] = {1200, -400, 400, -1200, 1200, -400,  400,  -1200,
                                         1200, -400, 400, -1200, 400,  -1200, 1200, -12400};

static void mount_card(void)
{
    ui_port_worker_inline(true);
    ui_port_storage_clear_files();
    ui_port_storage_set(STARK_STORAGE_MOUNTED, 1u << 20, 1u << 20);
}

static void add_capture_file(const char *name, subghz_mod_t mod, uint32_t hz)
{
    static char text[2048];
    subghz_raw_t r = {hz, mod, 4800u, (subghz_sym_t *)k_frame, 16};
    size_t cursor = 0;
    size_t used = 0;
    for (;;) {
        size_t n = 0;
        TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_format(&r, &cursor, text + used, 256, &n));
        if (n == 0) {
            break;
        }
        used += n;
    }
    text[used] = '\0';
    ui_port_storage_add_file(name, text);
}

/* DOWN `downs` times from the top of the home list, then OK, then a frame for the worker. */
static void open_home_row(int downs)
{
    for (int i = 0; i < downs; i++) {
        press(STARK_KEY_DOWN);
    }
    press(STARK_KEY_OK);
    frame();
}

/* Home -> Capture -> Start (the Start row is the third row of the setup). */
static void start_capture(void)
{
    open_home_row(2);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK);
}

static void test_capture_setup_start_stop_save_and_again(void)
{
    ui_port_subghz_reset();
    ui_port_subghz_set_ready(true);
    mount_card();
    launch();
    open_home_row(2);                              /* Capture */
    TEST_ASSERT_FALSE(ui_port_subghz_receiving()); /* the setup receives nothing */
    TEST_ASSERT_EQUAL(0, ui_port_subghz_capture_starts());
    TEST_ASSERT_NULL(sgfile_symbols()); /* and holds no buffer yet */
    pin("cap_setup", UINT64_C(0xe2f906eb009221dd));

    press(STARK_KEY_RIGHT); /* Frequency: 433.945 */
    press(STARK_KEY_RIGHT); /* 433.970 */
    press(STARK_KEY_DOWN);
    press(STARK_KEY_RIGHT); /* Modulation: 2-FSK */
    pin("cap_setup_2fsk", UINT64_C(0xef6b91abcc3c745d));
    press(STARK_KEY_LEFT); /* OOK again */
    press(STARK_KEY_RIGHT);
    TEST_ASSERT_EQUAL(0, ui_port_subghz_meter_starts()); /* the setup never retunes the meter */
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK); /* Start */
    TEST_ASSERT_EQUAL(1, ui_port_subghz_capture_starts());
    TEST_ASSERT_EQUAL(SUBGHZ_MOD_2FSK, ui_port_subghz_capture_mod());
    TEST_ASSERT_EQUAL_UINT32(433970000u, ui_port_subghz_capture_hz());
    TEST_ASSERT_TRUE(ui_port_subghz_receiving());
    TEST_ASSERT_NOT_NULL(sgfile_symbols()); /* the 8 kB buffer is taken now */
    pin("cap_running_empty", UINT64_C(0x1c7b12c9f0737d4e));

    const subghz_sym_t silence[1] = {-3000};
    ui_port_subghz_capture_feed(silence, 1); /* leading silence is dropped */
    ui_port_subghz_capture_feed(k_frame, 16);
    frame();
    pin("cap_running", UINT64_C(0x073148bb8a1d619e));
    press(STARK_KEY_OK); /* Stop: the service is asked, and answers when it has idled the radio */
    TEST_ASSERT_EQUAL(1, ui_port_subghz_capture_finishes());
    ui_port_subghz_capture_end();
    frame();
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
    TEST_ASSERT_TRUE(ui_port_log_contains("subghzapp: capture 16 sym 24800 us"));
    TEST_ASSERT_TRUE(ui_port_log_contains("subghzapp: hist 400x7 1200x8 12400x1"));
    pin("cap_done", UINT64_C(0x1ee1d06223e7d8b4));

    press(STARK_KEY_OK); /* Save */
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("subghzapp: saved rf_0001.rf (16 sym) hash 0x"));
    const char *text = ui_port_storage_file("rf_0001.rf");
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_EQUAL_STRING_LEN(
        "stark-subghz 1\nfreq 433970000\nmod 2fsk\nrate 4800\nsymbols 16\n", text, 58);
    subghz_sym_t back[SUBGHZ_RAW_MAX_SYMBOLS];
    subghz_raw_t r;
    TEST_ASSERT_EQUAL(STARK_OK, subghz_raw_parse(text, strlen(text), back, 2048, &r));
    TEST_ASSERT_EQUAL(16, r.n);
    TEST_ASSERT_EQUAL_INT32_ARRAY(k_frame, back, 16); /* exactly what was captured */
    pin("cap_saved", UINT64_C(0xf1a7993bd315cde6));

    press(STARK_KEY_OK); /* Save again: the next number */
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("subghzapp: saved rf_0002.rf (16 sym)"));
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK); /* Capture again */
    TEST_ASSERT_EQUAL(2, ui_port_subghz_capture_starts());
    TEST_ASSERT_TRUE(ui_port_subghz_receiving());
    pin("cap_again", UINT64_C(0x1c7b12c9f0737d4e)); /* running, nothing saved yet for this one */
    press(STARK_KEY_BACK);                          /* leaves the view and stops the radio */
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
    press(STARK_KEY_BACK);
    TEST_ASSERT_NULL(sgfile_symbols()); /* the app is closed: the buffer is back on the heap */
}

static void test_a_capture_that_cannot_be_saved(void)
{
    ui_port_subghz_reset();
    ui_port_subghz_set_ready(true);
    mount_card();
    launch();
    start_capture(); /* OOK at 433.920 MHz, the defaults */
    TEST_ASSERT_EQUAL(SUBGHZ_MOD_OOK, ui_port_subghz_capture_mod());
    TEST_ASSERT_EQUAL_UINT32(433920000u, ui_port_subghz_capture_hz());
    ui_port_subghz_capture_end(); /* nothing was heard */
    frame();
    pin("cap_done_empty", UINT64_C(0x75cce9e458813dc4));
    press(STARK_KEY_OK); /* Save: there is nothing to save */
    TEST_ASSERT_TRUE(ui_port_log_contains("subghzapp: save refused: nothing captured"));
    TEST_ASSERT_NULL(ui_port_storage_file("rf_0001.rf"));
    press(STARK_KEY_BACK);

    /* a capture the card cannot hold: the write fails and nothing is left behind */
    start_capture();
    for (int i = 0; i < 100; i++) {
        ui_port_subghz_capture_feed(k_frame,
                                    16); /* 1600 symbols: more text than the test card holds */
    }
    ui_port_subghz_capture_end();
    frame();
    press(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("subghzapp: save failed:"));
    TEST_ASSERT_NULL(ui_port_storage_file("rf_0001.rf"));
    pin("cap_save_failed", UINT64_C(0x41c2e89c3eb8e8c4));
    press(STARK_KEY_BACK);

    /* no card at all */
    ui_port_storage_set(STARK_STORAGE_ABSENT, 0, 0);
    start_capture();
    ui_port_subghz_capture_feed(k_frame, 16);
    ui_port_subghz_capture_end();
    frame();
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("subghzapp: save refused: no card"));
    press(STARK_KEY_BACK);
    press(STARK_KEY_BACK);
}

static void test_the_saved_captures(void)
{
    ui_port_subghz_reset();
    ui_port_subghz_set_ready(true);
    mount_card();
    add_capture_file("rf_0001.rf", SUBGHZ_MOD_OOK, 433920000u);
    add_capture_file("rf_0003.rf", SUBGHZ_MOD_2FSK, 434250000u);
    ui_port_storage_add_file(
        "rf_0002.rf", "stark-subghz 1\nfreq 433920000\nmod ook\nrate 4800\nsymbols 3\n5 -6\n");
    ui_port_storage_add_file("notes.txt", "not a capture\n");
    ui_port_storage_add_file("rf_12.rf", "stark-subghz 1\n");
    launch();
    open_home_row(3); /* Saved */
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
    pin("saved_list",
        UINT64_C(0xacf37c967d0d6769)); /* newest first; the other files are not listed */

    press(STARK_KEY_OK); /* rf_0003.rf */
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("subghzapp: opened rf_0003.rf (16 sym) hash 0x"));
    TEST_ASSERT_TRUE(ui_port_log_contains("subghzapp: hist 400x7 1200x8 12400x1"));
    TEST_ASSERT_FALSE(ui_port_subghz_receiving()); /* reading a file never touches the radio */
    pin("file_view", UINT64_C(0x5ca351bf71d73b02));
    press(STARK_KEY_OK); /* nothing to activate */
    press(STARK_KEY_BACK);
    pin("saved_list_again", UINT64_C(0xacf37c967d0d6769)); /* back in the list, the same rows */

    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK); /* rf_0002.rf: fewer symbols than it declares */
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("subghzapp: open failed: rf_0002.rf: Invalid argument"));
    pin("saved_after_invalid",
        UINT64_C(0xacf37c967d0d6769)); /* still the list: a bad file never half-opens */
    press(STARK_KEY_DOWN);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK); /* rf_0001.rf */
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("subghzapp: opened rf_0001.rf (16 sym)"));
    press(STARK_KEY_BACK);
    press(STARK_KEY_BACK); /* the list goes home, the radio untouched */
    TEST_ASSERT_EQUAL(0, ui_port_subghz_capture_starts());
    press(STARK_KEY_BACK);
    TEST_ASSERT_NULL(sgfile_symbols());
}

static void test_saved_without_captures_or_card(void)
{
    ui_port_subghz_reset();
    ui_port_subghz_set_ready(true);
    mount_card();
    launch();
    open_home_row(3);
    pin("saved_empty", UINT64_C(0x6b12747ca8aea733));
    press(STARK_KEY_OK); /* nothing to open */
    press(STARK_KEY_BACK);
    press(STARK_KEY_BACK);

    ui_port_storage_set(STARK_STORAGE_ABSENT, 0, 0);
    launch();
    open_home_row(3);
    TEST_ASSERT_TRUE(ui_port_log_contains("subghzapp: saved: no card"));
    pin("saved_no_card", UINT64_C(0x6b12747ca8aea733));
    press(STARK_KEY_BACK);
    press(STARK_KEY_BACK);

    /* the card goes while the list is open: it is forgotten and the app goes home */
    ui_port_storage_set(STARK_STORAGE_MOUNTED, 1u << 20, 1u << 20);
    add_capture_file("rf_0001.rf", SUBGHZ_MOD_OOK, 433920000u);
    launch();
    open_home_row(3);
    pin("saved_one", UINT64_C(0xc6bdfc2aecf144af));
    ui_port_storage_set(STARK_STORAGE_REMOVED, 0, 0);
    frame();
    pin("saved_card_removed", UINT64_C(0xf9704708d9b0c017)); /* the home list */
    press(STARK_KEY_BACK);

    /* a worker that cannot start: the list is empty and the view says so */
    ui_port_storage_set(STARK_STORAGE_MOUNTED, 1u << 20, 1u << 20);
    ui_port_worker_inline(false);
    launch();
    open_home_row(3);
    TEST_ASSERT_TRUE(ui_port_log_contains("subghzapp: saved: Not supported"));
    press(STARK_KEY_OK);
    press(STARK_KEY_BACK);
    press(STARK_KEY_BACK);
    ui_port_worker_inline(true);
}

static void test_capture_views_survive_the_module_going_and_refusals(void)
{
    ui_port_subghz_reset();
    ui_port_subghz_set_ready(true);
    mount_card();
    launch();
    open_home_row(2);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_DOWN);
    ui_port_subghz_refuse(STARK_ERR_STATE); /* the module left before the UI was told */
    press(STARK_KEY_OK);
    TEST_ASSERT_EQUAL(0, ui_port_subghz_capture_starts());
    pin("cap_refused", UINT64_C(0xf9704708d9b0c017)); /* the home list */
    press(STARK_KEY_BACK);
    ui_port_subghz_refuse(STARK_OK);

    launch();
    start_capture();
    ui_port_subghz_capture_feed(k_frame, 16);
    frame();
    ui_port_subghz_set_ready(false); /* the module is lost while capturing */
    frame();
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
    pin("cap_lost", UINT64_C(0xd92189a11140edfa));
    press(STARK_KEY_OK);
    press(STARK_KEY_BACK);

    /* leaving the app (BACK held) from a running capture returns the radio to idle */
    ui_port_subghz_set_ready(true);
    launch();
    start_capture();
    TEST_ASSERT_TRUE(ui_port_subghz_receiving());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_pop_to_root());
    TEST_ASSERT_FALSE(ui_port_subghz_receiving());
    TEST_ASSERT_NULL(sgfile_symbols());
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
    RUN_TEST(test_capture_setup_start_stop_save_and_again);
    RUN_TEST(test_a_capture_that_cannot_be_saved);
    RUN_TEST(test_the_saved_captures);
    RUN_TEST(test_saved_without_captures_or_card);
    RUN_TEST(test_capture_views_survive_the_module_going_and_refusals);
    return UNITY_END();
}
