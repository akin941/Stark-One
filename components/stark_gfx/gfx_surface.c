/*
 * gfx_surface.c — surface setup and the shared clipping helper
 * (TASKS.md STARK-0013). Pure C, no allocation.
 */
#include <stddef.h>
#include "gfx_internal.h"

static int32_t max32(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static int32_t min32(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

void gfx_surface_init(gfx_surface_t *s, uint16_t *pixels, int16_t w, int16_t h, int16_t origin_x,
                      int16_t origin_y)
{
    if (s == NULL) {
        return;
    }
    *s = (gfx_surface_t){
        .pixels = pixels,
        .w = w,
        .h = h,
        .origin_x = origin_x,
        .origin_y = origin_y,
        .clip = {.x = origin_x, .y = origin_y, .w = w, .h = h},
    };
}

bool gfx_clip(const gfx_surface_t *s, int32_t x, int32_t y, int32_t w, int32_t h, gfx_span_t *out)
{
    if (s == NULL || s->pixels == NULL || w <= 0 || h <= 0 || s->w <= 0 || s->h <= 0) {
        return false;
    }

    /* Logical coordinates: the rectangle ∩ clip ∩ the buffer's area. */
    int32_t x0 = max32(x, max32(s->clip.x, s->origin_x));
    int32_t y0 = max32(y, max32(s->clip.y, s->origin_y));
    int32_t x1 = min32(x + w, min32(s->clip.x + s->clip.w, s->origin_x + s->w));
    int32_t y1 = min32(y + h, min32(s->clip.y + s->clip.h, s->origin_y + s->h));
    if (x0 >= x1 || y0 >= y1) {
        return false;
    }

    out->x0 = x0 - s->origin_x;
    out->y0 = y0 - s->origin_y;
    out->x1 = x1 - s->origin_x;
    out->y1 = y1 - s->origin_y;
    return true;
}
