/*
 * input_sample.h — private to stark_input: key GPIO levels -> raw bitmap.
 *
 * Split out of input_service.c so the pin order and the active-low polarity
 * are host-tested against the fake HAL (test/host/test_input_sample.c): the
 * target emulator cannot drive external GPIO inputs, so this is where that
 * mapping is proven before hardware (docs/VALIDATION.md).
 */
#pragma once

#include <stdint.h>
#include "stark_board.h"

/*
 * Reads each key pin through stark_hal and returns the raw bitmap
 * input_core_update() takes: bit k set = key k (stark_key_t order, the order
 * of pins->key[]) physically pressed. Keys are active-low (a pressed key
 * pulls its pin to ground); a key the board does not have (-1) reads as
 * released (ARCHITECTURE §6.1).
 */
uint8_t input_sample_raw(const stark_board_pins_t *pins);
