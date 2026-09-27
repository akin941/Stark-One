/*
 * gfx_draw.c — primitives (TASKS.md STARK-0013). Each one clips through
 * gfx_clip() and then writes only inside the span it returns.
 */
#include <stddef.h>
#include "gfx_internal.h"

static uint16_t *row_at(const gfx_surface_t *s, int32_t y)
{
    return s->pixels + (size_t)y * (size_t)s->w;
}

static void fill32(gfx_surface_t *s, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t colour)
{
    gfx_span_t sp;
    if (!gfx_clip(s, x, y, w, h, &sp)) {
        return;
    }
    for (int32_t by = sp.y0; by < sp.y1; by++) {
        uint16_t *row = row_at(s, by);
        for (int32_t bx = sp.x0; bx < sp.x1; bx++) {
            row[bx] = colour;
        }
    }
}

void gfx_fill(gfx_surface_t *s, gfx_rect_t r, uint16_t colour)
{
    fill32(s, r.x, r.y, r.w, r.h, colour);
}

void gfx_hline(gfx_surface_t *s, int16_t x, int16_t y, int16_t w, uint16_t colour)
{
    fill32(s, x, y, w, 1, colour);
}

void gfx_vline(gfx_surface_t *s, int16_t x, int16_t y, int16_t h, uint16_t colour)
{
    fill32(s, x, y, 1, h, colour);
}

void gfx_rect(gfx_surface_t *s, gfx_rect_t r, uint16_t colour)
{
    int32_t x = r.x, y = r.y, w = r.w, h = r.h;
    if (w <= 0 || h <= 0) {
        return;
    }
    fill32(s, x, y, w, 1, colour); /* top */
    if (h > 1) {
        fill32(s, x, y + h - 1, w, 1, colour); /* bottom */
    }
    if (h > 2) {
        fill32(s, x, y + 1, 1, h - 2, colour); /* left */
        if (w > 1) {
            fill32(s, x + w - 1, y + 1, 1, h - 2, colour); /* right */
        }
    }
}

void gfx_blit_1bpp(gfx_surface_t *s, int16_t x, int16_t y, const uint8_t *bits, int16_t w,
                   int16_t h, uint16_t fg, uint16_t bg, bool transparent)
{
    gfx_span_t sp;
    if (bits == NULL || !gfx_clip(s, x, y, w, h, &sp)) {
        return;
    }

    size_t stride = ((size_t)w + 7u) / 8u;
    /* Source coordinates of the span's top-left corner. */
    int32_t src_x0 = sp.x0 + s->origin_x - x;
    int32_t src_y0 = sp.y0 + s->origin_y - y;
    for (int32_t by = sp.y0; by < sp.y1; by++) {
        const uint8_t *src = bits + (size_t)(src_y0 + (by - sp.y0)) * stride;
        uint16_t *row = row_at(s, by);
        for (int32_t bx = sp.x0; bx < sp.x1; bx++) {
            int32_t sx = src_x0 + (bx - sp.x0);
            bool on = ((src[sx >> 3] >> (7 - (sx & 7))) & 1u) != 0;
            if (on) {
                row[bx] = fg;
            } else if (!transparent) {
                row[bx] = bg;
            }
        }
    }
}
