/*
 * stark_hal_host.c — fake HAL for host tests.
 * Implements components/stark_hal/include/stark_hal.h exactly (the same
 * header stark_hal_esp.c implements — STARK-0007 AC #1); test-only
 * control/inspection lives in stark_hal_host_control.h instead.
 */
#include "stark_hal.h"
#include "stark_hal_host_control.h"
#include <string.h>

/* ---- Time --------------------------------------------------------------- */

static uint64_t g_fake_now_us = 0;

uint64_t stark_hal_now_us(void)
{
    return g_fake_now_us;
}

void stark_hal_delay_ms(uint32_t ms)
{
    (void)ms; /* no-op in host tests — tests never sleep */
}

void hal_host_set_now_us(uint64_t now_us)
{
    g_fake_now_us = now_us;
}

/* ---- GPIO --------------------------------------------------------------- */

#define MAX_GPIO 49 /* ESP32-S3: GPIO0-48 */

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

/* ---- PWM (LEDC) -----------------------------------------------------------
 * No fake hardware to drive — these just record their arguments for tests
 * to inspect (TASKS.md STARK-0007 host-test requirement). */

static hal_host_pwm_init_call_t g_pwm_init_call;
static hal_host_pwm_set_freq_call_t g_pwm_set_freq_call;
static hal_host_pwm_set_duty_call_t g_pwm_set_duty_call;

stark_err_t stark_hal_pwm_init(int pin, uint32_t hz, uint8_t channel)
{
    g_pwm_init_call.called = true;
    g_pwm_init_call.pin = pin;
    g_pwm_init_call.hz = hz;
    g_pwm_init_call.channel = channel;
    return STARK_OK;
}

stark_err_t stark_hal_pwm_set_freq(uint8_t channel, uint32_t hz)
{
    g_pwm_set_freq_call.called = true;
    g_pwm_set_freq_call.channel = channel;
    g_pwm_set_freq_call.hz = hz;
    return STARK_OK;
}

stark_err_t stark_hal_pwm_set_duty_pct(uint8_t channel, uint8_t pct)
{
    if (pct > 100) {
        pct = 100;
    }
    g_pwm_set_duty_call.called = true;
    g_pwm_set_duty_call.channel = channel;
    g_pwm_set_duty_call.pct = pct;
    return STARK_OK;
}

const hal_host_pwm_init_call_t *hal_host_pwm_last_init(void)
{
    return &g_pwm_init_call;
}

const hal_host_pwm_set_freq_call_t *hal_host_pwm_last_set_freq(void)
{
    return &g_pwm_set_freq_call;
}

const hal_host_pwm_set_duty_call_t *hal_host_pwm_last_set_duty(void)
{
    return &g_pwm_set_duty_call;
}

void hal_host_pwm_reset(void)
{
    memset(&g_pwm_init_call, 0, sizeof(g_pwm_init_call));
    memset(&g_pwm_set_freq_call, 0, sizeof(g_pwm_set_freq_call));
    memset(&g_pwm_set_duty_call, 0, sizeof(g_pwm_set_duty_call));
}
