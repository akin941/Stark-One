/*
 * stark_display.h — L3, owns the ILI9341 panel (ARCHITECTURE.md §6.5).
 *
 * STARK-0015: panel bring-up and a test pattern. The band render API
 * (stark_display_render()) is STARK-0016. No ESP-IDF types in this header.
 */
#pragma once

#include <stdint.h>
#include "stark_err.h"

/*
 * Attaches the panel to the SPI2 bus stark_board_init() created (so call
 * that first), resets and initialises it in the Kconfig orientation
 * (320x240 landscape by default), turns the backlight on (LEDC channel 1)
 * and allocates the DMA band buffer. Errors are translated to stark_err_t;
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
 * STARK-0015 bring-up only: eight colour bars (red, green, blue, yellow,
 * cyan, magenta, white, black, left to right), a 1 px white border and a
 * 12x12 white orientation marker in the top-left corner. STARK-0016
 * replaces this with a render callback drawing the same pattern.
 */
stark_err_t stark_display_test_pattern(void);
