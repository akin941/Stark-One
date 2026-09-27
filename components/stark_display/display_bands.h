/*
 * display_bands.h — private to stark_display: the band-walk arithmetic of
 * stark_display_render(), pure so it can be host-tested (TASKS.md
 * STARK-0016).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_gfx.h"

/*
 * Clips `area` to the screen (scr_w x scr_h) and splits the result along
 * the fixed band grid (rows [k*band_h, (k+1)*band_h)). Writes one rectangle
 * per intersected band, top to bottom — each is the part of the clipped
 * area inside that band, i.e. exactly the pixels that band renders and
 * transfers, and its (x, y) is the band surface's origin. Returns how many
 * were written: 0 for an empty or fully off-screen area, a non-positive
 * size or band_h, or max_out == 0; never more than max_out.
 */
size_t display_bands(gfx_rect_t area, int16_t scr_w, int16_t scr_h, int16_t band_h, gfx_rect_t *out,
                     size_t max_out);
