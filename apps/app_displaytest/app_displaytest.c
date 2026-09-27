/*
 * app_displaytest.c — Display Test (TASKS.md STARK-0020): colour bars, a
 * gradient, a 1 px grid, text samples (ASCII; Latin-1/Turkish in both fonts,
 * STARK-0102) and a live FPS figure. It redraws
 * its whole area every frame and reports frames per second — displayed and
 * logged, informational only (the FPS gate is a hardware criterion,
 * ADR-0011).
 */
#include <stdio.h>
#include "gfx_font.h"
#include "stark_app.h"
#include "stark_hal.h"
#include "stark_log.h"

#define CONTENT        ((gfx_rect_t){0, 16, 320, 224})

/* The Latin-1/Turkish sample (STARK-0102), drawn in both fonts. */
#define DT_SAMPLE_UTF8 "ÇĞİÖŞÜ çğıöşü äéñß"
#define DT_SAMPLE_X    8
#define DT_SAMPLE16_Y  180
#define DT_SAMPLE10_Y  200

static unsigned s_frames;
static uint64_t s_window_start_us;
static unsigned s_fps_x10;
static stark_screen_t s_screen;

static void count_frame(void)
{
    s_frames++;
    uint64_t now = stark_hal_now_us();
    uint64_t elapsed = now - s_window_start_us;
    if (elapsed >= 1000000u) {
        s_fps_x10 = (unsigned)((uint64_t)s_frames * 10000000u / elapsed);
        STARK_LOGI("displaytest", "fps=%u.%u", s_fps_x10 / 10, s_fps_x10 % 10);
        s_frames = 0;
        s_window_start_us = now;
    }
}

static void display_render(stark_screen_t *self, gfx_surface_t *s)
{
    static const uint16_t bars[8] = {
        GFX_RGB565(255, 0, 0),     GFX_RGB565(0, 255, 0),   GFX_RGB565(0, 0, 255),
        GFX_RGB565(255, 255, 0),   GFX_RGB565(0, 255, 255), GFX_RGB565(255, 0, 255),
        GFX_RGB565(255, 255, 255), GFX_RGB565(0, 0, 0),
    };
    for (int16_t i = 0; i < 8; i++) {
        gfx_fill(s, (gfx_rect_t){(int16_t)(i * 40), 16, 40, 48}, bars[i]);
    }
    for (int16_t x = 0; x < 320; x++) { /* grey ramp */
        uint8_t v = (uint8_t)(x * 255 / 319);
        gfx_vline(s, x, 64, 32, GFX_RGB565(v, v, v));
    }
    gfx_fill(s, (gfx_rect_t){0, 96, 320, 64}, GFX_RGB565(0, 0, 0));
    for (int16_t x = 0; x < 320; x += 16) { /* 1 px grid */
        gfx_vline(s, x, 96, 64, GFX_RGB565(0, 160, 0));
    }
    for (int16_t y = 96; y < 160; y += 16) {
        gfx_hline(s, 0, y, 320, GFX_RGB565(0, 160, 0));
    }
    gfx_fill(s, (gfx_rect_t){0, 160, 320, 80}, GFX_RGB565(0, 0, 0));
    (void)gfx_text(s, &gfx_font_mono16, 8, 162, "The quick brown fox 0123456789", 0xFFFF, 0, true);
    (void)gfx_text(s, &gfx_font_mono16, DT_SAMPLE_X, DT_SAMPLE16_Y, DT_SAMPLE_UTF8, 0xFFFF, 0,
                   true);
    (void)gfx_text(s, &gfx_font_mono10, DT_SAMPLE_X, DT_SAMPLE10_Y, DT_SAMPLE_UTF8, 0xFFFF, 0,
                   true);
    char fps[24];
    snprintf(fps, sizeof fps, "FPS %u.%u", s_fps_x10 / 10, s_fps_x10 % 10);
    (void)gfx_text(s, &gfx_font_mono16, 8, 218, fps, GFX_RGB565(255, 255, 0), 0, true);

    /* The frame is done once its last band (the bottom row) is drawn; then
     * ask for the next one: a continuous full-area refresh. */
    if (s->origin_y + s->h >= 240) {
        count_frame();
        stark_ui_invalidate(self, CONTENT);
    }
}

static stark_err_t display_start(void **state)
{
    (void)state;
    s_frames = 0;
    s_fps_x10 = 0;
    s_window_start_us = stark_hal_now_us();
    return STARK_OK;
}

static stark_screen_t *display_screen(void *state)
{
    (void)state;
    s_screen = (stark_screen_t){.name = "Display Test", .on_render = display_render};
    return &s_screen;
}

const stark_app_t app_displaytest = {
    .id = "displaytest",
    .title = "Display Test",
    .category = "System",
    .on_start = display_start,
    .screen = display_screen,
};
