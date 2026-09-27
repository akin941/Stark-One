/*
 * display_bands.c — see display_bands.h. Pure C, 32-bit arithmetic.
 */
#include "display_bands.h"

static int32_t max32(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static int32_t min32(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

size_t display_bands(gfx_rect_t area, int16_t scr_w, int16_t scr_h, int16_t band_h, gfx_rect_t *out,
                     size_t max_out)
{
    if (out == NULL || max_out == 0 || scr_w <= 0 || scr_h <= 0 || band_h <= 0 || area.w <= 0 ||
        area.h <= 0) {
        return 0;
    }

    int32_t x0 = max32(area.x, 0);
    int32_t y0 = max32(area.y, 0);
    int32_t x1 = min32((int32_t)area.x + area.w, scr_w);
    int32_t y1 = min32((int32_t)area.y + area.h, scr_h);
    if (x0 >= x1 || y0 >= y1) {
        return 0;
    }

    size_t n = 0;
    for (int32_t band_top = (y0 / band_h) * band_h; band_top < y1 && n < max_out;
         band_top += band_h) {
        int32_t top = max32(y0, band_top);
        int32_t bottom = min32(y1, band_top + band_h);
        out[n++] = (gfx_rect_t){
            .x = (int16_t)x0,
            .y = (int16_t)top,
            .w = (int16_t)(x1 - x0),
            .h = (int16_t)(bottom - top),
        };
    }
    return n;
}
