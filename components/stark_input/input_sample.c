/*
 * input_sample.c — key GPIO levels -> raw bitmap (input_sample.h).
 * Uses only stark_hal, so it builds on the host against the fake HAL.
 */
#include "input_sample.h"
#include "stark_hal.h"

uint8_t input_sample_raw(const stark_board_pins_t *pins)
{
    uint8_t raw = 0;
    for (int k = 0; k < STARK_KEY_COUNT; k++) {
        if (pins->key[k] >= 0 && !stark_hal_gpio_read(pins->key[k])) {
            raw |= (uint8_t)(1u << k);
        }
    }
    return raw;
}
