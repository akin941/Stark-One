/*
 * gpioapp.h — private to the GPIO app (TASKS.md STARK-0702, ADR-0023): the rows of a pin's view
 * and the text they show, built from a pin's configuration and what stark_lab reports. gpio_views.c
 * holds no service call and no UI call (pure, host-tested to every line); app_gpio.c drives the
 * service and the screen.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_lab.h"

typedef enum {
    GPIOAPP_MODE_HIZ,
    GPIOAPP_MODE_IN,
    GPIOAPP_MODE_OUT,
    GPIOAPP_MODE_PWM,
    GPIOAPP_MODE_PULSE,
    GPIOAPP_MODES
} gpioapp_mode_t;

/* What the pin view is set to. Nothing in it drives the pad: only an action row does. */
typedef struct {
    gpioapp_mode_t mode;
    stark_lab_pull_t pull; /* Input */
    bool level;            /* Output: what Drive applies; Pulse: the level of the pulse */
    uint32_t hz;           /* PWM */
    uint32_t duty_pct;
    uint32_t width_us; /* Pulse */
} gpioapp_cfg_t;

typedef enum {
    GPIOVIEW_ROW_MODE,
    GPIOVIEW_ROW_PULL,
    GPIOVIEW_ROW_LEVEL,
    GPIOVIEW_ROW_FREQ,
    GPIOVIEW_ROW_DUTY,
    GPIOVIEW_ROW_WIDTH,
    GPIOVIEW_ROW_PROBE,   /* Hi-Z: what is the pad doing (floating / high / low) */
    GPIOVIEW_ROW_READ,    /* Input: read the level again */
    GPIOVIEW_ROW_MEASURE, /* Input: measure frequency and duty (STARK-0703) */
    GPIOVIEW_ROW_DRIVE,   /* Output: drive the Level row */
    GPIOVIEW_ROW_TOGGLE,  /* Output: flip the level and drive it */
    GPIOVIEW_ROW_PWM,     /* PWM: Start, or Stop while it runs */
    GPIOVIEW_ROW_PULSE,   /* Pulse: one pulse */
    GPIOVIEW_ROW_PAD,     /* the live state of the pad (dimmed, never selected) */
    GPIOVIEW_ROW_COUNT
} gpioview_row_t;

#define GPIOVIEW_ROWS_MAX 5 /* the longest views: mode, three settings or actions, the pad */

/* What the view shows besides the configuration. */
typedef struct {
    const gpioapp_cfg_t *cfg;
    const stark_lab_info_t *info; /* the service's view of the pin */
    bool level_read;              /* the last read of the pad (Input) */
    const char *probe;            /* "floating", "high", "low" or "" */
    const char *meas;             /* the Measure row's text: a result, "measuring..." or "" */
} gpioview_t;

void gpioview_cfg_reset(gpioapp_cfg_t *c); /* Hi-Z, no pull, 1 kHz 50 %, 100 us, level low */

/* The row kinds of the view for this configuration, top to bottom; returns how many. */
size_t gpioview_rows(const gpioapp_cfg_t *c, gpioview_row_t *kinds);
const char *gpioview_label(gpioview_row_t k, bool pwm_running);

/* The value column of a row: a string in `buf` (or a constant); NULL for a row with none. */
const char *gpioview_value(gpioview_row_t k, const gpioview_t *v, char *buf, size_t n);

/*
 * LEFT (-1) / RIGHT (+1) on a row: true when the configuration changed. Mode cycles through the
 * five modes without wrapping past the ends; Pull, Level, Freq, Duty and Width step as
 * lab_pwm.h says. Action rows and the pad row do not adjust.
 */
bool gpioview_adjust(gpioapp_cfg_t *c, gpioview_row_t k, int dir);

/* "Hi-Z", "IN H", "IN L", "OUT H", "OUT L" or "PWM": the live state (level_read is the Input's). */
void gpioview_state_text(const stark_lab_info_t *info, bool level_read, char *buf, size_t n);

/* "38 LAB_UART_TX": a home row. */
void gpioview_home_label(int pin, const char *name, char *buf, size_t n);

/* "100 Hz", "1 kHz", "10 us", "1 ms", "1 s". */
void gpioview_freq_text(uint32_t hz, char *buf, size_t n);
void gpioview_width_text(uint32_t us, char *buf, size_t n);
