/*
 * stark_hal_host.h — fake HAL for host tests
 * Mirrors the stark_hal.h API but with test-controllable fakes.
 * This allows pure-core tests to run without ESP-IDF.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "stark_err.h"

/* ---- Time --------------------------------------------------------------- */

/**
 * Get current fake time in microseconds.
 * Tests must advance this explicitly via hal_host_set_now_us().
 */
uint64_t stark_hal_now_us(void);

/**
 * Delay function — in host tests this is a no-op.
 * Real delay would be implemented in the target HAL.
 */
void stark_hal_delay_ms(uint32_t ms);

/**
 * Set the fake clock value (microseconds since boot).
 * Used by tests to inject time.
 */
void hal_host_set_now_us(uint64_t now_us);

/* ---- GPIO --------------------------------------------------------------- */

/**
 * Configure a pin as input with optional pull-up.
 * Host implementation records the configuration for verification.
 */
stark_err_t stark_hal_gpio_config_input(int pin, bool pullup);

/**
 * Configure a pin as output with initial level.
 * Host implementation records the configuration for verification.
 */
stark_err_t stark_hal_gpio_config_output(int pin, bool initial);

/**
 * Read a GPIO pin level.
 * Returns the last value written via stark_hal_gpio_write()
 * or the initial value if configured as output.
 */
bool stark_hal_gpio_read(int pin);

/**
 * Write a GPIO pin level.
 * Host implementation records the value for verification.
 */
void stark_hal_gpio_write(int pin, bool level);

/**
 * Reset all GPIO state (for test isolation).
 */
void hal_host_gpio_reset(void);