/*
 * test_ui_storage.c — storage on the host UI test port (TASKS.md
 * STARK-0202): the status-bar glyph for every state (pixels against the
 * glyph's own bitmap; NONE leaves the V0.1 bar untouched and only the glyph
 * rectangle repaints on a change), the composition root's bridge
 * (main/storage_ui.c: state -> glyph, the "SD card removed" alert only on
 * an unexpected loss) and the real Storage app (status text, eject confirm,
 * mount request) against a settable stark_storage.
 */
#include <stdio.h>
#include <string.h>
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_theme.h"
#include "stark_ui.h"
#include "storage_ui.h"
#include "ui_port.h"
#include "ui_port_storage.h"
#include "unity.h"

extern const stark_app_t app_storage;
static const stark_app_t *const k_apps[] = {&app_storage};

/* The glyph's rectangle (ui_statusbar.c): 8 x 10 at the bar's right end. */
#define GX (UI_PORT_W - STARK_THEME_TITLE_X - 8)
#define GY ((STARK_THEME_STATUSBAR_H - 10) / 2)

static const uint8_t k_filled[10] = {0x3F, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint8_t k_outline[10] = {0x3F, 0x41, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF};

void setUp(void)
{}
void tearDown(void)
{}

static void frame(void)
{
    stark_ui_tick();
}

static void set_state(stark_storage_state_t st)
{
    ui_port_reset_records();
    ui_port_storage_set(st, 64ull << 20, 48ull << 20);
    frame();
}

/* Every pixel of the glyph rectangle: set bits in `fg`, the rest bar colour. */
static void assert_glyph(const uint8_t *bits, uint16_t fg)
{
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 8; x++) {
            bool on = bits != NULL && (bits[y] & (0x80u >> x)) != 0;
            uint16_t want = on ? fg : STARK_THEME_STATUS_BG;
            if (ui_port_pixel(GX + x, GY + y) != want) {
                char msg[64];
                snprintf(msg, sizeof msg, "glyph pixel %d,%d", x, y);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

static void test_status_glyph_and_bridge(void)
{
    hal_host_set_now_us(0);
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, 1));
    TEST_ASSERT_EQUAL(STARK_OK, storage_ui_init());
    frame();
    assert_glyph(NULL, 0); /* no card slot reported yet: the V0.1 bar */
    uint64_t v01_bar = ui_port_fb_hash();

    set_state(STARK_STORAGE_NOT_FITTED);
    TEST_ASSERT_EQUAL_UINT64(v01_bar, ui_port_fb_hash());
    TEST_ASSERT_EQUAL(0, ui_port_render_count()); /* nothing changed: nothing drawn */

    set_state(STARK_STORAGE_ABSENT);
    assert_glyph(k_outline, STARK_THEME_STATUS_DIM);
    TEST_ASSERT_EQUAL(1, ui_port_render_count()); /* just the glyph */
    gfx_rect_t a = ui_port_render_area(0);
    TEST_ASSERT_EQUAL(GX, a.x);
    TEST_ASSERT_EQUAL(GY, a.y);
    TEST_ASSERT_EQUAL(8, a.w);
    TEST_ASSERT_EQUAL(10, a.h);

    set_state(STARK_STORAGE_MOUNTED);
    assert_glyph(k_filled, STARK_THEME_STATUS_FG);
    set_state(STARK_STORAGE_EJECTED);
    assert_glyph(k_outline, STARK_THEME_STATUS_FG);
    TEST_ASSERT_FALSE(ui_port_log_contains("dialog: open")); /* eject is not a loss */
    set_state(STARK_STORAGE_UNUSABLE);
    assert_glyph(k_filled, STARK_THEME_ERROR);
    TEST_ASSERT_FALSE(ui_port_log_contains("dialog: open"));

    /* Only MOUNTED -> REMOVED raises the alert. */
    set_state(STARK_STORAGE_MOUNTED);
    set_state(STARK_STORAGE_REMOVED);
    assert_glyph(k_filled, STARK_THEME_ERROR);
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"SD card removed\""));
    ui_port_short(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: ok"));
    ui_port_reset_records();
    set_state(STARK_STORAGE_REMOVED); /* still removed: no second alert */
    TEST_ASSERT_FALSE(ui_port_log_contains("dialog: open"));
}

static void test_storage_app(void)
{
    set_state(STARK_STORAGE_MOUNTED);
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("storage"));
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("storageapp: status mounted"));
    TEST_ASSERT_TRUE(ui_port_log_contains("app: start storage"));

    /* The first enabled row is Eject (the status row is not selectable). */
    ui_port_short(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Eject card?\""));
    ui_port_short(STARK_KEY_BACK); /* cancel: nothing requested */
    frame();
    TEST_ASSERT_EQUAL(0, ui_port_storage_ejects());
    ui_port_short(STARK_KEY_OK);
    frame();
    ui_port_short(STARK_KEY_OK); /* confirm */
    frame();
    TEST_ASSERT_EQUAL(1, ui_port_storage_ejects());
    TEST_ASSERT_TRUE(ui_port_log_contains("storageapp: eject requested"));

    /* The storage task's answer updates the status row. */
    ui_port_reset_records();
    ui_port_storage_set(STARK_STORAGE_EJECTED, 0, 0);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("storageapp: status ejected"));
    TEST_ASSERT_TRUE(ui_port_render_count() > 0);

    /* Eject when not mounted asks nothing; Mount is always a request. */
    ui_port_reset_records();
    ui_port_short(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("storageapp: eject: not mounted"));
    TEST_ASSERT_FALSE(ui_port_log_contains("dialog: open"));
    ui_port_short(STARK_KEY_DOWN);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("menu: sel=2 \"Mount\""));
    ui_port_short(STARK_KEY_OK);
    frame();
    TEST_ASSERT_EQUAL(1, ui_port_storage_mounts());

    /* Every state has its status text. */
    static const stark_storage_state_t states[] = {STARK_STORAGE_ABSENT,     STARK_STORAGE_REMOVED,
                                                   STARK_STORAGE_UNUSABLE,   STARK_STORAGE_EJECTED,
                                                   STARK_STORAGE_NOT_FITTED, STARK_STORAGE_MOUNTED};
    for (size_t i = 0; i < sizeof states / sizeof states[0]; i++) {
        ui_port_reset_records();
        ui_port_storage_set(states[i], 64ull << 20, 48ull << 20);
        frame();
        char want[48];
        snprintf(want, sizeof want, "storageapp: status %s", stark_storage_state_str(states[i]));
        TEST_ASSERT_TRUE_MESSAGE(ui_port_log_contains(want), want);
    }
    /* The Storage app, mounted, Mount selected (inspected; regenerate the PPM
     * by changing the hash, then look before pinning a new one). */
    uint64_t h = ui_port_fb_hash();
    if (h != UINT64_C(0xd14de48693a42d56)) {
        (void)ui_port_dump_ppm("ui_storage_mounted.ppm");
        printf("storage frame hash 0x%016llx (see ui_storage_mounted.ppm)\n",
               (unsigned long long)h);
    }
    TEST_ASSERT_EQUAL_HEX64(UINT64_C(0xd14de48693a42d56), h);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_status_glyph_and_bridge);
    RUN_TEST(test_storage_app);
    return UNITY_END();
}
