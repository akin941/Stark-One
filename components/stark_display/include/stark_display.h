/*
 * stark_display.h — L3, owns the ILI9341 panel (ARCHITECTURE.md §6.5).
 *
 * Panel bring-up (STARK-0015) and the band render API every screen draws
 * through (STARK-0016). No ESP-IDF types in this header.
 */
#pragma once

#include <stdint.h>
#include "stark_err.h"
#include "stark_gfx.h"

/*
 * Attaches the panel to the SPI2 bus stark_board_init() created (so call
 * that first), resets and initialises it in the Kconfig orientation
 * (320x240 landscape by default), turns the backlight on (LEDC channel 1)
 * and allocates the two DMA band buffers (all display allocation happens
 * here). Logs `display: init 320x240 band=40 bufs=2x25600`. Errors are translated to stark_err_t;
 * the caller treats a failure as boot-critical (stark_panic()).
 * STARK_ERR_STATE if already initialised.
 */
stark_err_t stark_display_init(void);

/* Logical size after rotation: 320 x 240. */
int16_t stark_display_width(void);
int16_t stark_display_height(void);

/* 0..100 %, clamped. STARK_ERR_STATE before init. */
stark_err_t stark_display_set_backlight(uint8_t pct);

/*
 * Render callback: called once per band with a surface whose origin and
 * clip cover exactly the part of the requested area inside that band. The
 * callee draws in logical screen coordinates; anything outside is clipped.
 */
typedef void (*stark_render_fn)(gfx_surface_t *surface, void *ctx);

/*
 * Redraws `area` (clipped to the screen; empty or off-screen = no-op): walks
 * only the bands it intersects, calls fn once per band into one of two DMA
 * buffers — the next band renders while the previous one transfers — and
 * returns once every transfer has finished, so only the pixels in `area`
 * change. A full-screen render logs `display: full refresh <N> ms`
 * (informational). UI task only; not re-entrant. STARK_ERR_STATE before
 * init, STARK_ERR_INVALID_ARG for a NULL fn, STARK_ERR_TIMEOUT if a
 * transfer never completes.
 */
stark_err_t stark_display_render(gfx_rect_t area, stark_render_fn fn, void *ctx);
