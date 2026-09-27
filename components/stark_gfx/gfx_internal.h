/*
 * gfx_internal.h — private to components/stark_gfx: the one clipping
 * helper every primitive goes through (TASKS.md STARK-0013).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stark_gfx.h"

/* A clipped span in buffer coordinates: [x0, x1) x [y0, y1), non-empty. */
typedef struct {
    int32_t x0, y0, x1, y1;
} gfx_span_t;

/*
 * Intersects the logical rectangle (x, y, w, h) with the surface's clip and
 * with the logical area its buffer covers, and converts the result to
 * buffer coordinates. Returns false — draw nothing — for a NULL surface or
 * buffer, an empty rectangle, or no overlap. 32-bit arithmetic throughout,
 * so x + w cannot overflow int16_t.
 */
bool gfx_clip(const gfx_surface_t *s, int32_t x, int32_t y, int32_t w, int32_t h, gfx_span_t *out);
