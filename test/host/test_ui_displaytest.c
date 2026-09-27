/*
 * test_ui_displaytest.c — the real Display Test screen on the host UI test
 * port (TASKS.md STARK-0102, docs/VALIDATION.md §3.3): its Latin-1/Turkish
 * sample rows, in both fonts, are pixel-identical to a direct gfx_text()
 * render of the same string. This is the display-boundary replacement for
 * a screenshot; the panel glass is a HIL check.
 */
#include <stdio.h>
#include <string.h>
#include "gfx_font.h"
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_ui.h"
#include "ui_port.h"
#include "unity.h"

extern const stark_app_t app_displaytest;
static const stark_app_t *const k_apps[] = {&app_displaytest};

const stark_app_t *const *app_registry(size_t *count);
const stark_app_t *const *app_registry(size_t *count)
{
    *count = 1;
    return k_apps;
}

/* Must match apps/app_displaytest/app_displaytest.c. */
#define SAMPLE   "ÇĞİÖŞÜ çğıöşü äéñß"
#define SAMPLE_X 8

void setUp(void)
{}
void tearDown(void)
{}

/* The framebuffer rows [y, y + f->h) equal gfx_text(f, SAMPLE) drawn at
 * (SAMPLE_X, y) over black, white ink — across the full width. */
static void assert_row_is_sample(const gfx_font_t *f, int16_t y)
{
    static uint16_t expect[UI_PORT_W * 16];
    memset(expect, 0, sizeof expect);
    gfx_surface_t s;
    gfx_surface_init(&s, expect, UI_PORT_W, f->h, 0, y);
    TEST_ASSERT_EQUAL_INT16(gfx_text_width(f, SAMPLE),
                            gfx_text(&s, f, SAMPLE_X, y, SAMPLE, 0xFFFF, 0, true));
    size_t ink = 0;
    for (int16_t r = 0; r < f->h; r++) {
        for (int16_t x = 0; x < UI_PORT_W; x++) {
            uint16_t want = expect[r * UI_PORT_W + x];
            uint16_t got = ui_port_pixel(x, y + r);
            if (want != got) {
                char msg[64];
                snprintf(msg, sizeof msg, "pixel %d,%d: want %04x got %04x", x, y + r, want, got);
                TEST_FAIL_MESSAGE(msg);
            }
            ink += want == 0xFFFF ? 1u : 0u;
        }
    }
    TEST_ASSERT_TRUE(ink > 100); /* the row really holds text */
}

static void test_displaytest_sample_rows(void)
{
    hal_host_set_now_us(0);
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init());
    stark_ui_tick();
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("displaytest"));
    stark_ui_tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: start displaytest"));
    (void)ui_port_dump_ppm("ui_displaytest.ppm");

    assert_row_is_sample(&gfx_font_mono16, 180);
    assert_row_is_sample(&gfx_font_mono10, 200);
    TEST_ASSERT_EQUAL_INT16(18 * 8, gfx_text_width(&gfx_font_mono16, SAMPLE)); /* 18 cps */
    TEST_ASSERT_EQUAL_INT16(18 * 6, gfx_text_width(&gfx_font_mono10, SAMPLE));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_displaytest_sample_rows);
    return UNITY_END();
}
