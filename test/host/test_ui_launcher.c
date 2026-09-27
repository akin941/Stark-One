/*
 * test_ui_launcher.c — host UI test port (Tier 4B, docs/VALIDATION.md): the
 * production launcher, list menu, status bar and screen stack, driven by key
 * events, asserted on pixels, render areas, log lines and buzzer calls.
 *
 * This replaces the Wokwi screenshots of V0 at the display boundary: what is
 * checked is exactly what the firmware hands to stark_display_render(). The
 * panel itself (orientation, colour order, glass) is a HIL check.
 *
 * stark_ui and stark_app are init-once singletons (as in the firmware), so
 * the executable tells one story in order; each step asserts its own state.
 */
#include <stdio.h>
#include <string.h>
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_ui.h"
#include "ui_port.h"
#include "ui_theme.h"
#include "unity.h"

/* ---- the registry: four apps shaped like V0's ------------------------- */

static int s_started, s_stopped;
static stark_screen_t s_app_screen;
static uint64_t s_now_us;
static uint32_t s_render_cost_us; /* fake-clock time each app render "takes" */

static void app_render(stark_screen_t *self, gfx_surface_t *s)
{
    (void)self;
    gfx_fill(s, s->clip, GFX_RGB565(0, 0, 255));
    s_now_us += s_render_cost_us;
    hal_host_set_now_us(s_now_us);
}

static stark_err_t app_start(void **state)
{
    (void)state;
    s_started++;
    return STARK_OK;
}

static void app_stop(void *state)
{
    (void)state;
    s_stopped++;
}

static stark_screen_t *app_screen(void *state)
{
    (void)state;
    return &s_app_screen;
}

#define TEST_APP(sym, id_, title_)                                                                 \
    static const stark_app_t sym = {.id = id_,                                                     \
                                    .title = title_,                                               \
                                    .category = "System",                                          \
                                    .on_start = app_start,                                         \
                                    .on_stop = app_stop,                                           \
                                    .screen = app_screen}
TEST_APP(k_about, "about", "About");
TEST_APP(k_input, "inputtest", "Input Test");
TEST_APP(k_display, "displaytest", "Display Test");
TEST_APP(k_buzzer, "buzzertest", "Buzzer Test");

static const stark_app_t *const k_apps[] = {&k_about, &k_input, &k_display, &k_buzzer};

/* app_registry.c's contract (app_internal.h), provided by the test. */
const stark_app_t *const *app_registry(size_t *count);
const stark_app_t *const *app_registry(size_t *count)
{
    *count = sizeof k_apps / sizeof k_apps[0];
    return k_apps;
}

/* ---- helpers ---------------------------------------------------------- */

#define ROW_Y(i) (UI_STATUSBAR_H + (i) * UI_MENU_ROW_H)

static void frame(void)
{
    stark_ui_tick();
}

static void assert_area(size_t i, int x, int y, int w, int h)
{
    gfx_rect_t r = ui_port_render_area(i);
    char msg[64];
    snprintf(msg, sizeof msg, "render %u: %d,%d %dx%d", (unsigned)i, r.x, r.y, r.w, r.h);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(x, r.x, msg);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(y, r.y, msg);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(w, r.w, msg);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(h, r.h, msg);
}

static void assert_selected_row(int row)
{
    for (int i = 0; i < 4; i++) {
        uint16_t bg = ui_port_pixel(UI_PORT_W - 2, ROW_Y(i) + 1);
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(i == row ? UI_COLOR_SEL_BG : UI_COLOR_BG, bg,
                                        "row background (selection inverted)");
    }
}

static void assert_no_poison(void)
{
    const uint16_t *fb = ui_port_fb();
    for (size_t i = 0; i < UI_PORT_W * UI_PORT_H; i++) {
        if (fb[i] == UI_PORT_POISON) {
            char msg[48];
            snprintf(msg, sizeof msg, "undrawn pixel at %u,%u", (unsigned)(i % UI_PORT_W),
                     (unsigned)(i / UI_PORT_W));
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

/* The committed golden of the V0 launcher's first frame (reviewed as
 * build-host/ui_launcher.ppm when it was recorded). A deliberate visual
 * change updates it in the same commit. */
#define LAUNCHER_GOLDEN UINT64_C(0x63872b499bbce143)

void setUp(void)
{}
void tearDown(void)
{}

static void test_launcher_story(void)
{
    hal_host_set_now_us(0);
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init());
    TEST_ASSERT_TRUE(
        ui_port_log_contains("app: registry n=4 about,inputtest,displaytest,buzzertest"));
    s_app_screen = (stark_screen_t){.name = "App", .on_render = app_render};

    /* First frame: one full-screen render, every pixel drawn. */
    ui_port_reset_records();
    frame();
    TEST_ASSERT_EQUAL_UINT(1, ui_port_render_count());
    assert_area(0, 0, 0, UI_PORT_W, UI_PORT_H);
    assert_no_poison();
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_STATUS_BG, ui_port_pixel(UI_PORT_W - 1, 0));
    TEST_ASSERT_EQUAL_HEX16(UI_COLOR_STATUS_BG, ui_port_pixel(UI_PORT_W - 1, UI_STATUSBAR_H - 1));
    assert_selected_row(0);
    (void)ui_port_dump_ppm("ui_launcher.ppm"); /* uploaded by CI on failure */
    uint64_t first = ui_port_fb_hash();
    if (first != LAUNCHER_GOLDEN) {
        printf("launcher frame hash 0x%016llx (see ui_launcher.ppm)\n", (unsigned long long)first);
    }
    TEST_ASSERT_EQUAL_HEX64(LAUNCHER_GOLDEN, first);

    /* Nothing changed: no render at all. */
    ui_port_reset_records();
    frame();
    TEST_ASSERT_EQUAL_UINT(0, ui_port_render_count());

    /* DOWN: sel 0 -> 1, the two adjacent rows repaint (one union rect). */
    ui_port_key(STARK_KEY_DOWN, STARK_KEY_PRESS);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("menu: sel=1 \"Buzzer Test\""));
    TEST_ASSERT_EQUAL_INT(1, ui_port_buzzer_clicks());
    TEST_ASSERT_EQUAL_UINT(1, ui_port_render_count());
    assert_area(0, 0, ROW_Y(0), UI_PORT_W, 2 * UI_MENU_ROW_H);
    assert_selected_row(1);

    /* UP twice: back to 0, then wrap to the last row. The wrap repaints the
     * two rows involved, not the list between them (STARK-0101). */
    ui_port_reset_records();
    ui_port_key(STARK_KEY_UP, STARK_KEY_PRESS);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("menu: sel=0 \"About\""));
    ui_port_reset_records();
    ui_port_key(STARK_KEY_UP, STARK_KEY_PRESS);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("menu: sel=3 \"Input Test\""));
    TEST_ASSERT_EQUAL_UINT(2, ui_port_render_count());
    assert_area(0, 0, ROW_Y(0), UI_PORT_W, UI_MENU_ROW_H);
    assert_area(1, 0, ROW_Y(3), UI_PORT_W, UI_MENU_ROW_H);
    assert_selected_row(3);

    /* OK (short) launches the selected app; its screen and title redraw. */
    ui_port_reset_records();
    ui_port_short(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: start inputtest"));
    TEST_ASSERT_EQUAL_INT(1, s_started);
    assert_area(0, 0, 0, UI_PORT_W, UI_PORT_H);
    TEST_ASSERT_EQUAL_HEX16(GFX_RGB565(0, 0, 255), ui_port_pixel(160, 120));

    /* BACK (short) stops it; the launcher returns, selection kept, and the
     * full redraw with a different band height is pixel-identical to the
     * earlier frame at that selection (band boundaries leave no seams). */
    ui_port_reset_records();
    ui_port_set_band_h(7);
    ui_port_short(STARK_KEY_BACK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop inputtest"));
    TEST_ASSERT_EQUAL_INT(1, s_stopped);
    assert_no_poison();
    assert_selected_row(3);
    uint64_t banded7 = ui_port_fb_hash();

    ui_port_reset_records();
    ui_port_set_band_h(UI_PORT_H); /* one band: the whole screen at once */
    ui_port_short(STARK_KEY_OK);
    frame();
    ui_port_short(STARK_KEY_BACK);
    frame();
    TEST_ASSERT_EQUAL_HEX64(banded7, ui_port_fb_hash());
    ui_port_set_band_h(40);

    /* BACK at the root: nothing to pop, a reject buzz, no render. */
    ui_port_reset_records();
    ui_port_short(STARK_KEY_BACK);
    frame();
    TEST_ASSERT_EQUAL_INT(1, ui_port_buzzer_rejects());
    TEST_ASSERT_EQUAL_UINT(0, ui_port_render_count());

    /* A failed render is reported and counted, never fatal. */
    stark_ui_stats_t before, after;
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_stats(&before));
    ui_port_reset_records();
    ui_port_display_fail_next();
    ui_port_key(STARK_KEY_DOWN, STARK_KEY_PRESS);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("ui: render failed: "));
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_stats(&after));
    TEST_ASSERT_EQUAL_UINT32(before.render_errors + 1, after.render_errors);
    TEST_ASSERT_EQUAL_UINT32(before.frames + 1, after.frames);
    TEST_ASSERT_EQUAL_UINT32(before.renders + ui_port_render_count(), after.renders);
}

/* Frame statistics on the fake clock (STARK-0101). Budget: 1 000 000 / 30 µs. */
static void test_frame_statistics(void)
{
    stark_ui_stats_t s0, s1;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_ui_stats(NULL));
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_stats(&s0));

    /* An idle tick is not a frame. */
    frame();
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_stats(&s1));
    TEST_ASSERT_EQUAL_UINT32(s0.frames, s1.frames);

    /* Launch: the app's render "takes" 50 ms -> one overrun, recorded as max. */
    s_render_cost_us = 50000;
    ui_port_reset_records();
    ui_port_short(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: start "));
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_stats(&s1));
    TEST_ASSERT_EQUAL_UINT32(s0.frames + 1, s1.frames);
    TEST_ASSERT_EQUAL_UINT32(s0.overruns + 1, s1.overruns);
    TEST_ASSERT_TRUE(s1.last_frame_us >= 50000);
    TEST_ASSERT_TRUE(s1.max_frame_us >= 50000);
    TEST_ASSERT_EQUAL_UINT32(s0.renders + ui_port_render_count(), s1.renders);

    /* Within the budget: 20 ms for a region inside one band (the render
     * callback runs once per band): a frame, no overrun, max unchanged. */
    s_render_cost_us = 20000;
    stark_ui_invalidate(&s_app_screen, (gfx_rect_t){0, 16, 320, 20});
    stark_ui_stats_t s2;
    frame();
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_stats(&s2));
    TEST_ASSERT_EQUAL_UINT32(s1.frames + 1, s2.frames);
    TEST_ASSERT_EQUAL_UINT32(s1.overruns, s2.overruns);
    TEST_ASSERT_EQUAL_UINT32(s1.max_frame_us, s2.max_frame_us);
    TEST_ASSERT_TRUE(s2.last_frame_us >= 20000 && s2.last_frame_us <= 33333);

    s_render_cost_us = 0;
    ui_port_short(STARK_KEY_BACK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop "));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_launcher_story);
    RUN_TEST(test_frame_statistics); /* continues the story's state */
    return UNITY_END();
}
