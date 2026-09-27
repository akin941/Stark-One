/*
 * stark_gfx.h — L2, pure 2D drawing into caller-provided RGB565 memory
 * (ARCHITECTURE.md §6.4, TASKS.md STARK-0013).
 *
 * No hardware knowledge, no allocation. Every primitive takes *logical
 * screen* coordinates; the surface maps them onto its buffer through
 * origin_x/origin_y, which is what lets stark_display render the screen one
 * band at a time without callers knowing. Pixels are native-endian RGB565 —
 * the panel's byte order is stark_display's concern (STARK-0015).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 8-bit channels -> native-endian RGB565. */
#define GFX_RGB565(r, g, b)                                                                        \
    ((uint16_t)((((uint16_t)(r) & 0xF8u) << 8) | (((uint16_t)(g) & 0xFCu) << 3) |                  \
                ((uint16_t)(b) >> 3)))

/* A rectangle; w or h <= 0 is empty. */
typedef struct {
    int16_t x, y, w, h;
} gfx_rect_t;

/*
 * A window onto the logical screen: pixels[] (w * h, row-major) holds the
 * logical area [origin_x, origin_x + w) x [origin_y, origin_y + h). `clip`
 * is in logical coordinates too; drawing is limited to clip ∩ that area.
 */
typedef struct {
    uint16_t *pixels;
    int16_t w, h;
    int16_t origin_x, origin_y;
    gfx_rect_t clip;
} gfx_surface_t;

/* Wraps caller memory; clip starts as the whole logical area it covers. */
void gfx_surface_init(gfx_surface_t *s, uint16_t *pixels, int16_t w, int16_t h, int16_t origin_x,
                      int16_t origin_y);

/* Every primitive clips through one helper and silently draws nothing for a
 * NULL surface, NULL pixels/bits, empty sizes or a fully clipped area. */
void gfx_fill(gfx_surface_t *s, gfx_rect_t r, uint16_t colour);
void gfx_rect(gfx_surface_t *s, gfx_rect_t r, uint16_t colour); /* 1 px outline */
void gfx_hline(gfx_surface_t *s, int16_t x, int16_t y, int16_t w, uint16_t colour);
void gfx_vline(gfx_surface_t *s, int16_t x, int16_t y, int16_t h, uint16_t colour);

/*
 * Draws a w x h 1-bit image with its top-left at (x, y). `bits` is
 * row-major, each row padded to a whole byte, most significant bit = the
 * leftmost pixel. Set bits are painted fg; clear bits are painted bg, or
 * left untouched when `transparent`.
 */
void gfx_blit_1bpp(gfx_surface_t *s, int16_t x, int16_t y, const uint8_t *bits, int16_t w,
                   int16_t h, uint16_t fg, uint16_t bg, bool transparent);
