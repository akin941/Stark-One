/*
 * fake_lab.c — the test-only model of the lab's wires (fake_lab.h). Test host and emulator
 * test image only; production never links it.
 */
#include "fake_lab.h"
#include <string.h>

#define MAX_WIRES 4

typedef struct {
    fake_lab_mode_t mode;
    uint8_t pull;
    bool out;
    fake_lab_ext_t ext;
    uint32_t hz; /* the external square wave */
    uint8_t duty;
    uint32_t pwm_hz; /* the PWM output */
    uint8_t pwm_duty;
} pad_t;

typedef struct {
    fake_lab_fault_t kind;
    int pin;
    int count; /* -1 forever */
} fault_t;

static pad_t g_pad[FAKE_LAB_PINS];
static int g_wire_from[MAX_WIRES];
static int g_wire_to[MAX_WIRES];
static int g_wires;
static fault_t g_fault;
static uint64_t g_now_us;
static unsigned g_calls;
static unsigned g_pulses;
static uint32_t g_pulse_us;
static bool g_pulse_level;

static bool valid(int pin)
{
    return pin >= 0 && pin < FAKE_LAB_PINS;
}

void fake_lab_reset(void)
{
    memset(g_pad, 0, sizeof(g_pad));
    memset(g_wire_from, 0, sizeof(g_wire_from));
    memset(g_wire_to, 0, sizeof(g_wire_to));
    g_wires = 0;
    g_fault = (fault_t){0};
    g_now_us = 0;
    g_calls = 0;
    g_pulses = 0;
    g_pulse_us = 0;
    g_pulse_level = false;
}

void fake_lab_time_us(uint64_t now_us)
{
    g_now_us = now_us;
}

void fake_lab_ext(int pin, fake_lab_ext_t mode, uint32_t hz, uint8_t duty_pct)
{
    if (valid(pin)) {
        g_pad[pin].ext = mode;
        g_pad[pin].hz = hz;
        g_pad[pin].duty = duty_pct;
    }
}

void fake_lab_wire(int from, int to)
{
    if (from < 0) {
        g_wires = 0;
    } else if (valid(from) && valid(to) && g_wires < MAX_WIRES) {
        g_wire_from[g_wires] = from;
        g_wire_to[g_wires] = to;
        g_wires++;
    }
}

void fake_lab_fault(fake_lab_fault_t fault, int pin, int count)
{
    g_fault =
        (fault_t){.kind = count == 0 ? FAKE_LAB_FAULT_NONE : fault, .pin = pin, .count = count};
}

/* True (and one use consumed) when `kind` is armed for this pad. */
static bool fault_hits(fake_lab_fault_t kind, int pin)
{
    if (g_fault.kind != kind || (g_fault.pin >= 0 && g_fault.pin != pin)) {
        return false;
    }
    if (g_fault.count > 0 && --g_fault.count == 0) {
        g_fault.kind = FAKE_LAB_FAULT_NONE;
    }
    return true;
}

bool fake_lab_square(uint32_t hz, uint8_t duty_pct, uint64_t now_us)
{
    uint64_t period = hz > 0 ? 1000000u / hz : 1u;
    if (period == 0) {
        period = 1;
    }
    return (now_us % period) * 100u < period * duty_pct;
}

/* The level an external source drives onto the pad now; false when nothing drives it. */
static bool external(int pin, bool *level)
{
    const pad_t *p = &g_pad[pin];
    switch (p->ext) {
        case FAKE_LAB_EXT_HIGH:
            *level = true;
            return true;
        case FAKE_LAB_EXT_LOW:
            *level = false;
            return true;
        case FAKE_LAB_EXT_SQUARE:
            *level = fake_lab_square(p->hz, p->duty, g_now_us);
            return true;
        case FAKE_LAB_EXT_NONE:
        default:
            break;
    }
    for (int i = 0; i < g_wires; i++) {
        if (g_wire_to[i] == pin && g_pad[g_wire_from[i]].mode == FAKE_LAB_PAD_OUT) {
            *level = g_pad[g_wire_from[i]].out;
            return true;
        }
        if (g_wire_to[i] == pin && g_pad[g_wire_from[i]].mode == FAKE_LAB_PAD_PWM) {
            *level = fake_lab_square(g_pad[g_wire_from[i]].pwm_hz, g_pad[g_wire_from[i]].pwm_duty,
                                     g_now_us);
            return true;
        }
    }
    return false;
}

stark_err_t fake_lab_pad_in(int pin, uint8_t pull)
{
    if (!valid(pin) || pull > 2u) {
        return STARK_ERR_INVALID_ARG;
    }
    g_calls++;
    if (fault_hits(FAKE_LAB_FAULT_CONFIG_ERROR, pin)) {
        return STARK_ERR_IO;
    }
    g_pad[pin].mode = FAKE_LAB_PAD_IN;
    g_pad[pin].pull = pull;
    return STARK_OK;
}

stark_err_t fake_lab_pad_out(int pin, bool level)
{
    if (!valid(pin)) {
        return STARK_ERR_INVALID_ARG;
    }
    g_calls++;
    if (fault_hits(FAKE_LAB_FAULT_CONFIG_ERROR, pin)) {
        return STARK_ERR_IO;
    }
    g_pad[pin].mode = FAKE_LAB_PAD_OUT;
    g_pad[pin].pull = 0;
    g_pad[pin].out = level;
    return STARK_OK;
}

stark_err_t fake_lab_pwm_start(int pin, uint32_t hz, uint8_t duty_pct)
{
    if (!valid(pin) || duty_pct > 100u) {
        return STARK_ERR_INVALID_ARG;
    }
    g_calls++;
    if (fault_hits(FAKE_LAB_FAULT_CONFIG_ERROR, pin)) {
        return STARK_ERR_IO;
    }
    g_pad[pin].mode = FAKE_LAB_PAD_PWM;
    g_pad[pin].pull = 0;
    g_pad[pin].pwm_hz = hz;
    g_pad[pin].pwm_duty = duty_pct;
    return STARK_OK;
}

stark_err_t fake_lab_pulse(int pin, bool level, uint32_t us)
{
    if (!valid(pin)) {
        return STARK_ERR_INVALID_ARG;
    }
    g_calls++;
    if (fault_hits(FAKE_LAB_FAULT_CONFIG_ERROR, pin)) {
        return STARK_ERR_IO;
    }
    g_pad[pin].mode = FAKE_LAB_PAD_OUT;
    g_pad[pin].pull = 0;
    g_pad[pin].out = !level; /* the pulse is over: the pad rests at the opposite level */
    g_pulses++;
    g_pulse_us = us;
    g_pulse_level = level;
    return STARK_OK;
}

stark_err_t fake_lab_pad_hiz(int pin)
{
    if (!valid(pin)) {
        return STARK_ERR_INVALID_ARG;
    }
    g_calls++;
    g_pad[pin].mode = FAKE_LAB_PAD_HIZ;
    g_pad[pin].pull = 0;
    g_pad[pin].out = false;
    g_pad[pin].pwm_hz = 0;
    g_pad[pin].pwm_duty = 0;
    return STARK_OK;
}

stark_err_t fake_lab_pad_read(int pin, bool *level)
{
    if (!valid(pin) || level == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    g_calls++;
    if (fault_hits(FAKE_LAB_FAULT_PAD_STUCK_LOW, pin)) {
        *level = false;
        return STARK_OK;
    }
    if (fault_hits(FAKE_LAB_FAULT_PAD_STUCK_HIGH, pin)) {
        *level = true;
        return STARK_OK;
    }
    const pad_t *p = &g_pad[pin];
    bool ext;
    bool driven = external(pin, &ext);
    if (p->mode == FAKE_LAB_PAD_OUT) {
        *level = driven ? ext : p->out; /* an external source against the output: contention */
    } else if (p->mode == FAKE_LAB_PAD_PWM) {
        *level = driven ? ext : fake_lab_square(p->pwm_hz, p->pwm_duty, g_now_us);
    } else if (driven) {
        *level = ext;
    } else {
        *level = p->pull == 1u; /* nothing drives it: the pull, else floating reads low */
    }
    return STARK_OK;
}

unsigned fake_lab_calls(void)
{
    return g_calls;
}

fake_lab_mode_t fake_lab_mode(int pin)
{
    return valid(pin) ? g_pad[pin].mode : FAKE_LAB_PAD_HIZ;
}

uint8_t fake_lab_pull(int pin)
{
    return valid(pin) ? g_pad[pin].pull : 0;
}

bool fake_lab_out_level(int pin)
{
    return valid(pin) && g_pad[pin].out;
}

uint32_t fake_lab_pwm_hz(int pin)
{
    return valid(pin) ? g_pad[pin].pwm_hz : 0u;
}

uint8_t fake_lab_pwm_duty(int pin)
{
    return valid(pin) ? g_pad[pin].pwm_duty : 0u;
}

unsigned fake_lab_pulses(void)
{
    return g_pulses;
}

uint32_t fake_lab_last_pulse_us(void)
{
    return g_pulse_us;
}

bool fake_lab_last_pulse_level(void)
{
    return g_pulse_level;
}
