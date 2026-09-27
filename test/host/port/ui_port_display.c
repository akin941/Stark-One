/*
 * ui_port_display.c — host UI test port: stark_display.h over a 320x240
 * RGB565 framebuffer. stark_display_render() walks the area with the
 * production band arithmetic (display_bands.c), hands each band surface to
 * the callback exactly as the target does, then copies the band into the
 * framebuffer. Band buffers start poisoned, so a pixel a screen failed to
 * draw inside its damage shows up as UI_PORT_POISON instead of stale data.
 */
#include <string.h>
#include "display_bands.h"
#include "ppm_dump.h"
#include "sdkconfig.h"
#include "stark_display.h"
#include "ui_port.h"

#define MAX_RENDERS 256
#define MAX_BANDS   UI_PORT_H

static uint16_t s_fb[UI_PORT_W * UI_PORT_H];
static uint16_t s_band[UI_PORT_W * UI_PORT_H];
static gfx_rect_t s_renders[MAX_RENDERS];
static size_t s_render_count;
static int16_t s_band_h = CONFIG_STARK_DISPLAY_BAND_H;
static bool s_fail_next;
static bool s_ready;

/* Clears the render record; the framebuffer keeps its pixels (a later
 * partial render must land on the previous frame, as on the panel). */
void ui_port_display_reset(void);
void ui_port_display_reset(void)
{
    s_render_count = 0;
    s_fail_next = false;
}

stark_err_t stark_display_init(void)
{
    if (s_ready) {
        return STARK_ERR_STATE;
    }
    for (size_t i = 0; i < UI_PORT_W * UI_PORT_H; i++) {
        s_fb[i] = UI_PORT_POISON; /* until the first frame */
    }
    ui_port_display_reset();
    s_ready = true;
    return STARK_OK;
}

int16_t stark_display_width(void)
{
    return UI_PORT_W;
}

int16_t stark_display_height(void)
{
    return UI_PORT_H;
}

stark_err_t stark_display_set_backlight(uint8_t pct)
{
    (void)pct;
    return s_ready ? STARK_OK : STARK_ERR_STATE;
}

stark_err_t stark_display_render(gfx_rect_t area, stark_render_fn fn, void *ctx)
{
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    if (fn == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_render_count < MAX_RENDERS) {
        s_renders[s_render_count++] = area;
    }
    if (s_fail_next) {
        s_fail_next = false;
        return STARK_ERR_TIMEOUT;
    }
    gfx_rect_t bands[MAX_BANDS];
    size_t n = display_bands(area, UI_PORT_W, UI_PORT_H, s_band_h, bands, MAX_BANDS);
    for (size_t i = 0; i < n; i++) {
        const gfx_rect_t *b = &bands[i];
        size_t px = (size_t)b->w * (size_t)b->h;
        for (size_t p = 0; p < px; p++) {
            s_band[p] = UI_PORT_POISON;
        }
        gfx_surface_t s;
        gfx_surface_init(&s, s_band, b->w, b->h, b->x, b->y);
        fn(&s, ctx);
        for (int16_t row = 0; row < b->h; row++) {
            memcpy(&s_fb[(size_t)(b->y + row) * UI_PORT_W + (size_t)b->x],
                   &s_band[(size_t)row * (size_t)b->w], (size_t)b->w * sizeof(uint16_t));
        }
    }
    return STARK_OK;
}

const uint16_t *ui_port_fb(void)
{
    return s_fb;
}

uint16_t ui_port_pixel(int x, int y)
{
    if (x < 0 || y < 0 || x >= UI_PORT_W || y >= UI_PORT_H) {
        return UI_PORT_POISON;
    }
    return s_fb[(size_t)y * UI_PORT_W + (size_t)x];
}

uint64_t ui_port_fb_hash(void)
{
    uint64_t h = 1469598103934665603u;
    for (size_t i = 0; i < UI_PORT_W * UI_PORT_H; i++) {
        h = (h ^ (s_fb[i] & 0xFFu)) * 1099511628211u;
        h = (h ^ (uint64_t)(s_fb[i] >> 8)) * 1099511628211u;
    }
    return h;
}

size_t ui_port_render_count(void)
{
    return s_render_count;
}

gfx_rect_t ui_port_render_area(size_t i)
{
    return i < s_render_count ? s_renders[i] : (gfx_rect_t){0, 0, 0, 0};
}

void ui_port_set_band_h(int16_t band_h)
{
    s_band_h = band_h;
}

void ui_port_display_fail_next(void)
{
    s_fail_next = true;
}

bool ui_port_dump_ppm(const char *path)
{
    return ppm_dump(path, s_fb, UI_PORT_W, UI_PORT_H);
}
