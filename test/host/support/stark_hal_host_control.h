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

/* ---- Time control -------------------------------------------------- */

/* Sets the fake clock (µs since boot) returned by stark_hal_now_us().
 * The clock never advances on its own — only this call moves it. */
void hal_host_set_now_us(uint64_t now_us);

/* ---- GPIO control ---------------------------------------------------- */

/* Clears all fake GPIO state (for test isolation between cases). */
void hal_host_gpio_reset(void);

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

const hal_host_pwm_init_call_t *hal_host_pwm_last_init(void);
const hal_host_pwm_set_freq_call_t *hal_host_pwm_last_set_freq(void);
const hal_host_pwm_set_duty_call_t *hal_host_pwm_last_set_duty(void);

/* Clears all recorded PWM calls (for test isolation between cases). */
void hal_host_pwm_reset(void);
