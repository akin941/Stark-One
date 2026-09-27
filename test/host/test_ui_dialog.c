/*
 * test_ui_dialog.c — the modal dialog on the host UI test port (TASKS.md
 * STARK-0105, docs/VALIDATION.md §3.3), driven through the real App Test
 * app and the public API: overlay pixels, key rules (a key held before the
 * dialog opened can never confirm), copied strings, done-chaining, BUSY.
 */
#include <stdio.h>
#include <string.h>
#include "gfx_font.h"
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_theme.h"
#include "stark_ui.h"
#include "ui_dialog.h"
#include "ui_dialog_layout.h"
#include "ui_port.h"
#include "unity.h"

extern const stark_app_t app_apptest;
static const stark_app_t *const k_apps[] = {&app_apptest};

void setUp(void)
{}
void tearDown(void)
{}

/* Index of the first captured log line containing needle at or after
 * `from`, or -1. */
static int log_find(const char *needle, size_t from)
{
    for (size_t i = from; i < ui_port_log_count(); i++) {
        if (strstr(ui_port_log_line(i), needle) != NULL) {
            return (int)i;
        }
    }
    return -1;
}

static void assert_log_order(const char *const *lines, size_t n)
{
    size_t from = 0;
    for (size_t i = 0; i < n; i++) {
        int at = log_find(lines[i], from);
        TEST_ASSERT_TRUE_MESSAGE(at >= 0, lines[i]);
        from = (size_t)at + 1;
    }
}

static void tick(void)
{
    stark_ui_tick();
}

static int s_done_calls;
static stark_dialog_result_t s_done_result;

static void record(stark_dialog_result_t r, void *ctx)
{
    (void)ctx;
    s_done_calls++;
    s_done_result = r;
}

static stark_screen_t s_other_overlay;
static void noop_render(stark_screen_t *self, gfx_surface_t *s)
{
    (void)self;
    (void)s;
}

static void test_dialog_story(void)
{
    hal_host_set_now_us(0);
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, 1));
    tick();
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("apptest"));
    tick();
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_ui_dialog_confirm(NULL, "m", NULL, NULL));

    /* Status bar before any dialog, to compare against. */
    static uint16_t status_before[UI_PORT_W * 16];
    memcpy(status_before, ui_port_fb(), sizeof status_before);

    /* "Confirm dialog" (selected) -> OK: the dialog opens over App Test. */
    ui_port_reset_records();
    ui_port_short(STARK_KEY_OK);
    tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Confirm\""));
    ui_dialog_layout_t lay;
    ui_dialog_layout(&gfx_font_mono16, &gfx_font_mono16, &gfx_font_mono10, "Confirm",
                     "Run the confirmed action? (App Test)", stark_ui_content_rect(), &lay);
    TEST_ASSERT_EQUAL_HEX16(STARK_THEME_ACCENT, ui_port_pixel(lay.box.x, lay.box.y));
    TEST_ASSERT_EQUAL_HEX16(STARK_THEME_ACCENT,
                            ui_port_pixel(lay.box.x + lay.box.w - 1, lay.box.y + lay.box.h - 1));
    TEST_ASSERT_EQUAL_HEX16(STARK_THEME_ACCENT, ui_port_pixel(lay.box.x + 1, lay.box.y + 1));
    TEST_ASSERT_EQUAL_HEX16(STARK_THEME_BG, ui_port_pixel(lay.box.x + 2, lay.box.y + 2));
    /* App Test's menu is still visible outside the box: row 0 selected. */
    TEST_ASSERT_EQUAL_HEX16(STARK_THEME_SEL_BG, ui_port_pixel(2, 17));
    TEST_ASSERT_EQUAL_HEX16(STARK_THEME_SEL_BG, ui_port_pixel(UI_PORT_W - 2, 17));
    /* The status bar keeps the beneath screen's title. */
    TEST_ASSERT_EQUAL_MEMORY(status_before, ui_port_fb(), sizeof status_before);
    /* The message was copied: App Test built it in a stack buffer that is
     * gone by now, yet the body holds exactly that text. */
    static uint16_t expect[UI_PORT_W * UI_PORT_H];
    memcpy(expect, ui_port_fb(), sizeof expect);
    gfx_surface_t es;
    gfx_surface_init(&es, expect, UI_PORT_W, UI_PORT_H, 0, 0);
    gfx_fill(&es, lay.body, STARK_THEME_BG);
    (void)gfx_text_box(&es, &gfx_font_mono16, lay.body, 0, "Run the confirmed action? (App Test)",
                       STARK_THEME_FG, STARK_THEME_BG, true);
    TEST_ASSERT_EQUAL_MEMORY(expect, ui_port_fb(), sizeof expect);
    (void)ui_port_dump_ppm("ui_dialog_confirm.ppm");

    /* One dialog at a time; no overlay on an overlay. */
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, stark_ui_dialog_alert("x", "y", NULL, NULL));
    s_other_overlay = (stark_screen_t){.name = "o", .on_render = noop_render, .overlay = true};
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, stark_ui_push(&s_other_overlay));

    /* Other keys are consumed and do nothing. */
    ui_port_key(STARK_KEY_DOWN, STARK_KEY_PRESS);
    ui_port_short(STARK_KEY_LEFT);
    tick();
    TEST_ASSERT_EQUAL(-1, log_find("menu: sel=", 0));

    /* BACK cancels; App Test stays. */
    ui_port_short(STARK_KEY_BACK);
    tick();
    static const char *const cancel[] = {"dialog: open \"Confirm\"", "dialog: cancel",
                                         "apptest: cancelled"};
    assert_log_order(cancel, 3);
    TEST_ASSERT_EQUAL(-1, log_find("app: stop", 0));

    /* A key held BEFORE the dialog opened can never confirm it. */
    ui_port_reset_records();
    s_done_calls = 0;
    ui_port_key(STARK_KEY_OK, STARK_KEY_PRESS);
    tick(); /* the menu sees the press (it acts on release) */
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_dialog_confirm("Held", "held OK", record, NULL));
    ui_port_key(STARK_KEY_OK, STARK_KEY_RELEASE);
    ui_port_key(STARK_KEY_OK, STARK_KEY_SHORT);
    tick();
    TEST_ASSERT_EQUAL_INT(0, s_done_calls);
    TEST_ASSERT_EQUAL(-1, log_find("dialog: ok", 0));
    ui_port_short(STARK_KEY_OK); /* a fresh press does */
    tick();
    TEST_ASSERT_EQUAL_INT(1, s_done_calls);
    TEST_ASSERT_EQUAL(STARK_DIALOG_OK, s_done_result);
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: ok"));

    /* Confirm with OK: done opens the "Done" alert (no BUSY); OK closes it. */
    ui_port_reset_records();
    ui_port_short(STARK_KEY_OK);
    tick();
    ui_port_short(STARK_KEY_OK);
    tick();
    ui_port_short(STARK_KEY_OK);
    tick();
    static const char *const confirm[] = {
        "dialog: open \"Confirm\"", "dialog: ok", "apptest: confirmed",
        "dialog: open \"Done\"",    "dialog: ok", "apptest: done closed"};
    assert_log_order(confirm, 6);

    /* "Alert dialog": BACK closes an alert too, with OK. */
    ui_port_reset_records();
    ui_port_key(STARK_KEY_DOWN, STARK_KEY_PRESS);
    tick();
    ui_port_short(STARK_KEY_OK);
    tick();
    ui_port_short(STARK_KEY_BACK);
    tick();
    static const char *const alert[] = {"menu: sel=1 \"Alert dialog\"", "dialog: open \"Alert\"",
                                        "dialog: ok", "apptest: alert closed"};
    assert_log_order(alert, 4);

    /* With no dialog left, BACK leaves App Test and the full screen redraws
     * with no undrawn pixel. */
    ui_port_reset_records();
    ui_port_short(STARK_KEY_BACK);
    tick();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop apptest"));
    const uint16_t *fb = ui_port_fb();
    for (size_t i = 0; i < UI_PORT_W * UI_PORT_H; i++) {
        TEST_ASSERT_NOT_EQUAL(UI_PORT_POISON, fb[i]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dialog_story);
    return UNITY_END();
}
