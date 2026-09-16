/*
 * stark_hal_host.c — fake HAL implementation for host tests
 * Provides a fake clock and fake GPIO array for deterministic testing.
 */
#include "stark_hal_host.h"
#include <string.h>

/* ---- Time --------------------------------------------------------------- */

static uint64_t g_fake_now_us = 0;

uint64_t stark_hal_now_us(void)
{
    return g_fake_now_us;
}

void stark_hal_delay_ms(uint32_t ms)
{
    (void)ms; /* no-op in host tests */
}

void hal_host_set_now_us(uint64_t now_us)
{
    g_fake_now_us = now_us;
}

/* ---- GPIO --------------------------------------------------------------- */

#define MAX_GPIO 48

typedef struct {
    bool configured;
    bool is_output;
    bool level;
    bool pullup;
} gpio_state_t;

static gpio_state_t g_gpio_state[MAX_GPIO];

stark_err_t stark_hal_gpio_config_input(int pin, bool pullup)
{
    if (pin < 0 || pin >= MAX_GPIO) {
        return STARK_ERR_INVALID_ARG;
    }
    g_gpio_state[pin].configured = true;
    g_gpio_state[pin].is_output = false;
    g_gpio_state[pin].pullup = pullup;
    g_gpio_state[pin].level = pullup; /* pulled high when floating */
    return STARK_OK;
}

stark_err_t stark_hal_gpio_config_output(int pin, bool initial)
{
    if (pin < 0 || pin >= MAX_GPIO) {
        return STARK_ERR_INVALID_ARG;
    }
    g_gpio_state[pin].configured = true;
    g_gpio_state[pin].is_output = true;
    g_gpio_state[pin].pullup = false;
    g_gpio_state[pin].level = initial;
    return STARK_OK;
}

bool stark_hal_gpio_read(int pin)
{
    if (pin < 0 || pin >= MAX_GPIO) {
        return false;
    }
    return g_gpio_state[pin].level;
}

void stark_hal_gpio_write(int pin, bool level)
{
    if (pin < 0 || pin >= MAX_GPIO) {
        return;
    }
    g_gpio_state[pin].level = level;
}

void hal_host_gpio_reset(void)
{
    memset(g_gpio_state, 0, sizeof(g_gpio_state));
}