/*
 * app_hello.c — Hello, the reference app (TASKS.md STARK-0104): one screen,
 * a greeting and the uptime, refreshed once a second. Adding it touched
 * only this directory and main/app_registry.{h,c} (ADR-0016).
 */
#include "esp_timer.h"
#include "gfx_font.h"
#include "stark_app.h"
#include "stark_log.h"
#include "stark_theme.h"
#include "ui_format.h"

#define UPTIME_DY 32 /* the uptime row, below the greeting */

static esp_timer_handle_t s_timer;
static stark_screen_t s_screen;
static char s_uptime[24];

static void on_second(void *arg)
{
    (void)arg;
    const stark_event_t tick = {.type = STARK_EVT_TICK};
    (void)stark_event_publish(&tick); /* the UI task redraws on it */
}

static void refresh(void)
{
    (void)ui_format_uptime((uint64_t)(esp_timer_get_time() / 1000000), s_uptime, sizeof s_uptime);
    STARK_LOGI("hello", "uptime=%s", s_uptime);
}

static bool hello_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type != STARK_EVT_TICK) {
        return false; /* BACK belongs to stark_ui */
    }
    refresh();
    gfx_rect_t c = stark_ui_content_rect();
    stark_ui_invalidate(self, (gfx_rect_t){0, (int16_t)(c.y + UPTIME_DY), c.w, 16});
    return true;
}

static void hello_render(stark_screen_t *self, gfx_surface_t *s)
{
    (void)self;
    gfx_rect_t c = stark_ui_content_rect();
    gfx_fill(s, s->clip, STARK_THEME_BG);
    (void)gfx_text(s, &gfx_font_mono16, STARK_THEME_TEXT_X, (int16_t)(c.y + 8), "Hello, STARK ONE",
                   STARK_THEME_FG, STARK_THEME_BG, true);
    (void)gfx_text(s, &gfx_font_mono16, STARK_THEME_TEXT_X, (int16_t)(c.y + UPTIME_DY), s_uptime,
                   STARK_THEME_ACTIVE, STARK_THEME_BG, true);
}

static stark_err_t hello_start(void **state)
{
    (void)state;
    refresh();
    const esp_timer_create_args_t args = {.callback = on_second, .name = "hello"};
    if (esp_timer_create(&args, &s_timer) != ESP_OK) {
        return STARK_ERR_NO_MEM;
    }
    if (esp_timer_start_periodic(s_timer, 1000000) != ESP_OK) {
        (void)esp_timer_delete(s_timer);
        return STARK_ERR_IO;
    }
    return STARK_OK;
}

static void hello_stop(void *state)
{
    (void)state;
    (void)esp_timer_stop(s_timer);
    (void)esp_timer_delete(s_timer);
}

static stark_screen_t *hello_screen(void *state)
{
    (void)state;
    s_screen =
        (stark_screen_t){.name = "Hello", .on_event = hello_event, .on_render = hello_render};
    return &s_screen;
}

/* Launcher icon (16x16, 1bpp — ui_menu.h). */
static const uint8_t k_icon[32] = {
    0x07, 0xE0, 0x18, 0x18, 0x20, 0x04, 0x40, 0x02, 0x46, 0x62, 0x86, 0x61, 0x80, 0x01, 0x80, 0x01,
    0x90, 0x09, 0x88, 0x11, 0x47, 0xE2, 0x40, 0x02, 0x20, 0x04, 0x18, 0x18, 0x07, 0xE0, 0x00, 0x00,
};

const stark_app_t app_hello = {
    .id = "hello",
    .title = "Hello",
    .category = "Examples",
    .on_start = hello_start,
    .on_stop = hello_stop,
    .screen = hello_screen,
    .icon = k_icon,
};
