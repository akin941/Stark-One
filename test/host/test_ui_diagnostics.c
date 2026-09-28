/*
 * test_ui_diagnostics.c — the real Diagnostics screen on the host UI test
 * port (TASKS.md STARK-0109, ROADMAP V0.1 exit 4): with known diag values,
 * the heap and FPS rows are pixel-identical to a direct render of the
 * expected text, and a new sample redraws exactly the rows that changed.
 */
#include <stdio.h>
#include <string.h>
#include "gfx_font.h"
#include "stark_app.h"
#include "stark_diag.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_theme.h"
#include "stark_ui.h"
#include "ui_port.h"
#include "ui_port_diag.h"
#include "unity.h"

extern const stark_app_t app_diagnostics;
static const stark_app_t *const k_apps[] = {&app_diagnostics};

void setUp(void)
{}
void tearDown(void)
{}

#define ROW_Y(i) (STARK_THEME_STATUSBAR_H + 4 + (i) * 12)

/* Framebuffer row i equals `text` drawn in mono10 at the text inset. */
static void assert_row(int i, const char *text, uint16_t fg)
{
    static uint16_t expect[UI_PORT_W * 12];
    for (size_t k = 0; k < sizeof expect / sizeof expect[0]; k++) {
        expect[k] = STARK_THEME_BG;
    }
    gfx_surface_t s;
    gfx_surface_init(&s, expect, UI_PORT_W, 10, 0, (int16_t)ROW_Y(i));
    (void)gfx_text(&s, &gfx_font_mono10, STARK_THEME_TEXT_X, (int16_t)ROW_Y(i), text, fg,
                   STARK_THEME_BG, true);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < UI_PORT_W; x++) {
            if (expect[y * UI_PORT_W + x] != ui_port_pixel(x, ROW_Y(i) + y)) {
                char msg[96];
                snprintf(msg, sizeof msg, "row %d (\"%s\") differs at %d,%d", i, text, x,
                         ROW_Y(i) + y);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

static void sample_event(void)
{
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_DIAG_SAMPLE;
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_publish(&e));
}

static void test_diagnostics_screen(void)
{
    stark_diag_snapshot_t snap = {
        .heap_free = 314312,
        .heap_min = 300000,
        .heap_largest = 250000,
        .fps_x10 = 305,
        .ui = {.frames = 1234, .overruns = 2, .render_errors = 0, .join_timeouts = 1},
        .ev_published = 500,
        .ev_dropped = 0,
        .ev_max_depth = 3,
        .uptime_s = 42,
    };
    const stark_diag_task_t tasks[] = {{"stark_ui", 3000}, {"IDLE0", 900}};
    ui_port_diag_set(&snap, tasks, 2);

    hal_host_set_now_us(0);
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, 1));
    stark_ui_tick();
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("diagnostics"));
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("diagnostics: heap=314312 fps=30.5"));
    assert_row(0, "Heap free   314312 B", STARK_THEME_ACTIVE);
    assert_row(3, "FPS         30.5", STARK_THEME_ACTIVE);
    assert_row(5, "Overruns    2", STARK_THEME_FG);
    assert_row(7, "Join t/o    1", STARK_THEME_FG);
    assert_row(8, "Events      500 pub 0 drop 3 peak", STARK_THEME_FG);
    assert_row(12, "stark_ui     3000", STARK_THEME_FG);
    assert_row(13, "IDLE0        900", STARK_THEME_FG);
    (void)ui_port_dump_ppm("ui_diagnostics.ppm");

    /* A new sample: heap and uptime changed — exactly those two rows redraw. */
    snap.heap_free = 314000;
    snap.uptime_s = 43;
    ui_port_diag_set(&snap, tasks, 2);
    ui_port_reset_records();
    sample_event();
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("diagnostics: heap=314000 fps=30.5"));
    TEST_ASSERT_EQUAL_UINT(2, ui_port_render_count());
    TEST_ASSERT_EQUAL_INT16(ROW_Y(0), ui_port_render_area(0).y);
    TEST_ASSERT_EQUAL_INT16(ROW_Y(9), ui_port_render_area(1).y);
    assert_row(0, "Heap free   314000 B", STARK_THEME_ACTIVE);
    assert_row(9, "Uptime      43 s", STARK_THEME_FG);

    /* An unchanged sample redraws nothing; BACK leaves. */
    ui_port_reset_records();
    sample_event();
    stark_ui_tick();
    TEST_ASSERT_EQUAL_UINT(0, ui_port_render_count());
    ui_port_short(STARK_KEY_BACK);
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop diagnostics"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_diagnostics_screen);
    return UNITY_END();
}
