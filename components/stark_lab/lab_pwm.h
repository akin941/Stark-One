/*
 * lab_pwm.h — the GPIO tool's PWM and pulse rules (TASKS.md STARK-0702, ADR-0023 §4): the range
 * and step rules, the pulse widths, and the LEDC arithmetic behind them. Pure: no lock, no
 * allocation, no ESP-IDF header. The limits are the transport's constants
 * (stark_lab_transport.h), written once; the host tests sweep them.
 *
 * lab_pwm_plan() models what the LEDC timer is asked to do (10-bit resolution on the 80 MHz
 * APB clock, a divider with 8 fractional bits) so the host can prove that every frequency in
 * range is reachable within a stated error and that every 1 % duty step is a distinct tick
 * count. ESP-IDF's own clock choice may do better; it is never worse than this plan, and the real
 * waveform is a V1 logic-analyser check (ROADMAP V1 exit 13).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_lab_transport.h"

#define LAB_PWM_DUTY_MAX    100u
#define LAB_PWM_APB_HZ      80000000u
#define LAB_PWM_RES_BITS    10u
#define LAB_PWM_MAX_TICK    ((1u << LAB_PWM_RES_BITS) - 1u) /* stark_hal_pwm_set_duty_pct() */
#define LAB_PWM_DIV_Q8_MIN  256u                            /* a divider of 1.0 */
#define LAB_PWM_DIV_Q8_MAX  ((1023u << 8) | 255u)           /* 10 integer + 8 fraction bits */
#define LAB_PWM_ERR_PPM_MAX 2000u /* 0.2 %: half a divider step at a divider of 1.0 */

#define LAB_PULSE_WIDTHS    6u

typedef struct {
    uint32_t divider_q8;   /* the LEDC clock divider, 8 fractional bits */
    uint32_t duty_ticks;   /* of LAB_PWM_MAX_TICK + 1, as stark_hal_pwm_set_duty_pct() sets it */
    uint32_t achieved_mhz; /* the frequency that divider gives, in millihertz */
    uint32_t error_ppm;    /* |achieved - requested| / requested, parts per million */
} lab_pwm_plan_t;

/* Whether the request is inside the fixed limits: 100 to 40 000 Hz, duty 0 to 100 %. */
bool lab_pwm_valid(uint32_t hz, uint32_t duty_pct);

/*
 * The LEDC plan for a request on the 80 MHz APB clock. False (out untouched) outside the limits
 * or when the divider would leave LEDC's range (it never does inside the limits; the host tests
 * sweep every frequency). The error never exceeds LAB_PWM_ERR_PPM_MAX.
 * lab_pwm_plan_clk() is the same for another source clock.
 */
bool lab_pwm_plan(uint32_t hz, uint32_t duty_pct, lab_pwm_plan_t *out);
bool lab_pwm_plan_clk(uint32_t clk_hz, uint32_t hz, uint32_t duty_pct, lab_pwm_plan_t *out);

/* The fixed pulse widths, in microseconds: 10, 100, 1000, 10000, 100000, 1000000. */
uint32_t lab_pulse_width(size_t index); /* 0 for an index past the list */
bool lab_pulse_valid(uint32_t us);
