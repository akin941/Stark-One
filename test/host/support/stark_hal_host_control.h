/*
 * stark_hal_host_control.h — test-only control surface for the fake HAL.
 *
 * Not part of stark_hal.h: there is no target-side equivalent of "set the
 * clock to an arbitrary value" or "inspect the last PWM call" — these
 * exist purely so tests can drive and inspect
 * test/host/support/stark_hal_host.c deterministically (TESTING.md: "No
 * test sleeps. Time is injected."). Only test files include this header;
 * production code (and stark_hal.h itself) never does.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "stark_hal.h"

/* ---- Time control -------------------------------------------------- */

/* Sets the fake clock (µs since boot) returned by stark_hal_now_us().
 * The clock never advances on its own — only this call moves it. */
void hal_host_set_now_us(uint64_t now_us);

/* The time stark_hal_delay_ms() and stark_hal_delay_us() were asked to wait since the last
 * hal_host_gpio_reset() (they never wait); the hook runs after each delay_ms with its 1-based
 * call number, so a test can act in the middle of a wait (NULL removes it). */
uint64_t hal_host_delayed_us(void);

/* Failures to inject (STARK-0702): stark_err IO from the calls named in the mask, until the mask is
 * changed or hal_host_gpio_reset() clears it. */
#define HAL_HOST_FAIL_PWM_INIT   1u
#define HAL_HOST_FAIL_PWM_DUTY   2u
#define HAL_HOST_FAIL_DRIVE_WEAK 4u
#define HAL_HOST_FAIL_OUT_WEAK   8u
void hal_host_fail(unsigned mask);
void hal_host_set_delay_hook(void (*hook)(unsigned call));

/* ---- GPIO control ---------------------------------------------------- */

/* Clears all fake GPIO state (for test isolation between cases). */
void hal_host_gpio_reset(void);

/* ---- Lab pad inspection (STARK-0701) ---------------------------------------
 * What the pad calls of stark_hal.h last did to a pin; mode is NONE until one has. */
typedef enum {
    HAL_HOST_PAD_NONE,
    HAL_HOST_PAD_IN,
    HAL_HOST_PAD_OUT,
    HAL_HOST_PAD_PWM /* stark_hal_pwm_init() routed it to LEDC */
} hal_host_pad_mode_t;

typedef struct {
    hal_host_pad_mode_t mode;
    stark_hal_pull_t pull; /* IN */
    bool weak;             /* OUT, PWM: the weakest drive strength was set */
    bool level;            /* OUT: the level it was set to */
} hal_host_pad_t;

/* NULL for a pin outside 0-48. */
const hal_host_pad_t *hal_host_pad(int pin);

/* How many pad calls (config_in, config_out_weak, hiz) reached the fake since the reset. */
unsigned hal_host_pad_calls(void);

/* ---- PWM inspection ---------------------------------------------------
 * Each struct records the arguments of the most recent matching call;
 * .called is false until that function has been invoked at least once. */

typedef struct {
    bool called;
    int pin;
    uint32_t hz;
    uint8_t channel;
} hal_host_pwm_init_call_t;

typedef struct {
    bool called;
    uint8_t channel;
    uint32_t hz;
} hal_host_pwm_set_freq_call_t;

typedef struct {
    bool called;
    uint8_t channel;
    uint8_t pct;
} hal_host_pwm_set_duty_call_t;

typedef struct {
    bool called;
    uint8_t channel;
} hal_host_pwm_stop_call_t;

const hal_host_pwm_stop_call_t *hal_host_pwm_last_stop(void);
unsigned hal_host_pwm_stops(void);
const hal_host_pwm_init_call_t *hal_host_pwm_last_init(void);
const hal_host_pwm_set_freq_call_t *hal_host_pwm_last_set_freq(void);
const hal_host_pwm_set_duty_call_t *hal_host_pwm_last_set_duty(void);

/* Clears all recorded PWM calls (for test isolation between cases). */
void hal_host_pwm_reset(void);
