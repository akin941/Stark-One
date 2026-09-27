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
#include "stark_theme.h"

/* The Latin-1/Turkish sample (STARK-0102), drawn in both fonts. Rows are
 * offsets from the top of the content area (stark_ui_content_rect()). */
#define DT_SAMPLE_UTF8 "ÇĞİÖŞÜ çğıöşü äéñß"
#define DT_ASCII_DY    146
#define DT_SAMPLE16_DY 164
#define DT_SAMPLE10_DY 184
#define DT_FPS_DY      202

/* The test pattern is this app's subject, so its colours are its own. */
#define DT_GRID_GREEN  GFX_RGB565(0, 160, 0)

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
    const gfx_rect_t c = stark_ui_content_rect();
    const int16_t bar_w = (int16_t)(c.w / 8);
    for (int16_t i = 0; i < 8; i++) {
        gfx_fill(s, (gfx_rect_t){(int16_t)(i * bar_w), c.y, bar_w, 48}, bars[i]);
    }
    for (int16_t x = 0; x < c.w; x++) { /* grey ramp */
        uint8_t v = (uint8_t)(x * 255 / (c.w - 1));
        gfx_vline(s, x, (int16_t)(c.y + 48), 32, GFX_RGB565(v, v, v));
    }
    const int16_t grid_y = (int16_t)(c.y + 80);
    gfx_fill(s, (gfx_rect_t){0, grid_y, c.w, 64}, STARK_THEME_BG);
    for (int16_t x = 0; x < c.w; x += 16) { /* 1 px grid */
        gfx_vline(s, x, grid_y, 64, DT_GRID_GREEN);
    }
    for (int16_t y = grid_y; y < grid_y + 64; y += 16) {
        gfx_hline(s, 0, y, c.w, DT_GRID_GREEN);
    }
    gfx_fill(s, (gfx_rect_t){0, (int16_t)(c.y + 144), c.w, (int16_t)(c.h - 144)}, STARK_THEME_BG);
    const int16_t tx = STARK_THEME_TEXT_X;
    (void)gfx_text(s, &gfx_font_mono16, tx, (int16_t)(c.y + DT_ASCII_DY),
                   "The quick brown fox 0123456789", STARK_THEME_FG, STARK_THEME_BG, true);
    (void)gfx_text(s, &gfx_font_mono16, tx, (int16_t)(c.y + DT_SAMPLE16_DY), DT_SAMPLE_UTF8,
                   STARK_THEME_FG, STARK_THEME_BG, true);
    (void)gfx_text(s, &gfx_font_mono10, tx, (int16_t)(c.y + DT_SAMPLE10_DY), DT_SAMPLE_UTF8,
                   STARK_THEME_FG, STARK_THEME_BG, true);
    char fps[24];
    snprintf(fps, sizeof fps, "FPS %u.%u", s_fps_x10 / 10, s_fps_x10 % 10);
    (void)gfx_text(s, &gfx_font_mono16, tx, (int16_t)(c.y + DT_FPS_DY), fps, STARK_THEME_ACTIVE,
                   STARK_THEME_BG, true);

    /* The frame is done once its last band (the bottom row) is drawn; then
     * ask for the next one: a continuous full-area refresh. */
    if (s->origin_y + s->h >= c.y + c.h) {
        count_frame();
        stark_ui_invalidate(self, c);
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

/* Launcher icon (16x16, 1bpp — ui_menu.h). */
static const uint8_t k_icon[32] = {
    0x00, 0x00, 0xFF, 0xFF, 0x80, 0x01, 0xBD, 0xB9, 0xBD, 0xB9, 0x80, 0x01, 0xB7, 0xB1, 0xB7, 0xB1,
    0x80, 0x01, 0x80, 0x01, 0xFF, 0xFF, 0x03, 0xC0, 0x03, 0xC0, 0x1F, 0xF8, 0x00, 0x00, 0x00, 0x00,
};

const stark_app_t app_displaytest = {
    .id = "displaytest",
    .title = "Display Test",
    .category = "Tests",
    .on_start = display_start,
    .screen = display_screen,
    .icon = k_icon,
};
