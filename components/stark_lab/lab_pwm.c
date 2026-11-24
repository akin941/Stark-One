/*
 * lab_pwm.c — the GPIO tool's PWM and pulse rules (lab_pwm.h). Pure; every function is a few
 * integer operations on its arguments.
 */
#include "lab_pwm.h"
#include "stark_lab.h"

static const uint32_t k_pulse_us[LAB_PULSE_WIDTHS] = {10u, 100u, 1000u, 10000u, 100000u, 1000000u};

bool lab_pwm_valid(uint32_t hz, uint32_t duty_pct)
{
    return hz >= STARK_LAB_PWM_HZ_MIN && hz <= STARK_LAB_PWM_HZ_MAX && duty_pct <= LAB_PWM_DUTY_MAX;
}

bool lab_pwm_plan_clk(uint32_t clk_hz, uint32_t hz, uint32_t duty_pct, lab_pwm_plan_t *out)
{
    if (!lab_pwm_valid(hz, duty_pct) || out == NULL) {
        return false;
    }
    /* divider = clk / (hz * 2^res), in Q8: clk * 256 / (hz * 1024) = clk / 4 / hz, rounded */
    uint32_t div_q8 = (clk_hz / 4u + hz / 2u) / hz;
    if (div_q8 < LAB_PWM_DIV_Q8_MIN || div_q8 > LAB_PWM_DIV_Q8_MAX) {
        return false;
    }
    /* achieved = clk / (divider * 2^res) = clk * 256 / (div_q8 * 1024), in millihertz. The error
     * is at most half a step of the divider, 1/512 of it at its smallest value of 1.0: 0.2 %. */
    uint64_t achieved_mhz = ((uint64_t)clk_hz * 1000u) / 4u / div_q8;
    uint64_t want_mhz = (uint64_t)hz * 1000u;
    uint64_t diff = achieved_mhz > want_mhz ? achieved_mhz - want_mhz : want_mhz - achieved_mhz;
    out->divider_q8 = div_q8;
    out->duty_ticks = (LAB_PWM_MAX_TICK * duty_pct) / LAB_PWM_DUTY_MAX;
    out->achieved_mhz = (uint32_t)achieved_mhz;
    out->error_ppm = (uint32_t)(diff * 1000000u / want_mhz);
    return true;
}

bool lab_pwm_plan(uint32_t hz, uint32_t duty_pct, lab_pwm_plan_t *out)
{
    return lab_pwm_plan_clk(LAB_PWM_APB_HZ, hz, duty_pct, out);
}

/* The largest power of ten that is not above v (v >= 1). */
static uint32_t decade(uint32_t v)
{
    uint32_t d = 1;
    while (d <= v / 10u) {
        d *= 10u;
    }
    return d;
}

uint32_t stark_lab_pwm_step_hz(uint32_t hz, int dir)
{
    uint32_t next = hz;
    if (dir > 0) {
        uint32_t unit = decade(hz < 1u ? 1u : hz);
        next = (hz / unit + 1u) * unit;
        if (next > STARK_LAB_PWM_HZ_MAX) {
            next = STARK_LAB_PWM_HZ_MAX;
        }
        if (next < STARK_LAB_PWM_HZ_MIN) {
            next = STARK_LAB_PWM_HZ_MIN;
        }
    } else if (dir < 0 && hz > STARK_LAB_PWM_HZ_MIN) {
        uint32_t below = hz > STARK_LAB_PWM_HZ_MAX ? STARK_LAB_PWM_HZ_MAX + 1u : hz;
        uint32_t unit = decade(below - 1u);
        /* NOTE: hz > MIN, so below - 1 >= MIN and the rung found is never under MIN */
        next = ((below - 1u) / unit) * unit;
    }
    return next;
}

uint32_t stark_lab_pwm_step_duty(uint32_t duty_pct, int dir)
{
    if (dir > 0 && duty_pct < LAB_PWM_DUTY_MAX) {
        return duty_pct + 1u;
    }
    if (dir < 0 && duty_pct > 0u) {
        return duty_pct - 1u;
    }
    return duty_pct;
}

uint32_t lab_pulse_width(size_t index)
{
    return index < LAB_PULSE_WIDTHS ? k_pulse_us[index] : 0u;
}

bool lab_pulse_valid(uint32_t us)
{
    for (size_t i = 0; i < LAB_PULSE_WIDTHS; i++) {
        if (k_pulse_us[i] == us) {
            return true;
        }
    }
    return false;
}

uint32_t stark_lab_pulse_step(uint32_t us, int dir)
{
    size_t at = 1; /* an unlisted value starts at 100 us */
    for (size_t i = 0; i < LAB_PULSE_WIDTHS; i++) {
        if (k_pulse_us[i] == us) {
            at = i;
        }
    }
    if (dir > 0 && at + 1u < LAB_PULSE_WIDTHS) {
        at++;
    } else if (dir < 0 && at > 0u) {
        at--;
    }
    return k_pulse_us[at];
}
