/*
 * gpio_views.c — the GPIO app's pin view (gpioapp.h, TASKS.md STARK-0702): which rows a mode
 * shows, what each says and what LEFT/RIGHT does to it. Pure: no service call, no UI call, no
 * allocation; every string is written into the caller's buffer or is a constant.
 */
#include <stdio.h>
#include "gpioapp.h"

static const char *const k_mode_text[GPIOAPP_MODES] = {"Hi-Z", "Input", "Output", "PWM", "Pulse"};
static const char *const k_pull_text[] = {"None", "Up", "Down"};

void gpioview_cfg_reset(gpioapp_cfg_t *c)
{
    *c = (gpioapp_cfg_t){.mode = GPIOAPP_MODE_HIZ,
                         .pull = STARK_LAB_PULL_NONE,
                         .level = false,
                         .hz = 1000u,
                         .duty_pct = 50u,
                         .width_us = 100u};
}

size_t gpioview_rows(const gpioapp_cfg_t *c, gpioview_row_t *kinds)
{
    size_t n = 0;
    kinds[n++] = GPIOVIEW_ROW_MODE;
    switch (c->mode) {
        case GPIOAPP_MODE_IN:
            kinds[n++] = GPIOVIEW_ROW_PULL;
            kinds[n++] = GPIOVIEW_ROW_READ;
            kinds[n++] = GPIOVIEW_ROW_MEASURE;
            break;
        case GPIOAPP_MODE_OUT:
            kinds[n++] = GPIOVIEW_ROW_LEVEL;
            kinds[n++] = GPIOVIEW_ROW_DRIVE;
            kinds[n++] = GPIOVIEW_ROW_TOGGLE;
            break;
        case GPIOAPP_MODE_PWM:
            kinds[n++] = GPIOVIEW_ROW_FREQ;
            kinds[n++] = GPIOVIEW_ROW_DUTY;
            kinds[n++] = GPIOVIEW_ROW_PWM;
            break;
        case GPIOAPP_MODE_PULSE:
            kinds[n++] = GPIOVIEW_ROW_LEVEL;
            kinds[n++] = GPIOVIEW_ROW_WIDTH;
            kinds[n++] = GPIOVIEW_ROW_PULSE;
            break;
        case GPIOAPP_MODE_HIZ:
        case GPIOAPP_MODES:
        default:
            kinds[n++] = GPIOVIEW_ROW_PROBE;
            break;
    }
    kinds[n++] = GPIOVIEW_ROW_PAD;
    return n;
}

const char *gpioview_label(gpioview_row_t k, bool pwm_running)
{
    switch (k) {
        case GPIOVIEW_ROW_MODE:
            return "Mode";
        case GPIOVIEW_ROW_PULL:
            return "Pull";
        case GPIOVIEW_ROW_LEVEL:
            return "Level";
        case GPIOVIEW_ROW_FREQ:
            return "Freq";
        case GPIOVIEW_ROW_DUTY:
            return "Duty";
        case GPIOVIEW_ROW_WIDTH:
            return "Width";
        case GPIOVIEW_ROW_PROBE:
            return "Probe";
        case GPIOVIEW_ROW_READ:
            return "Read";
        case GPIOVIEW_ROW_MEASURE:
            return "Measure";
        case GPIOVIEW_ROW_DRIVE:
            return "Drive";
        case GPIOVIEW_ROW_TOGGLE:
            return "Toggle";
        case GPIOVIEW_ROW_PWM:
            return pwm_running ? "Stop" : "Start";
        case GPIOVIEW_ROW_PULSE:
            return "Pulse";
        case GPIOVIEW_ROW_PAD:
        case GPIOVIEW_ROW_COUNT:
        default:
            return "Pad";
    }
}

void gpioview_freq_text(uint32_t hz, char *buf, size_t n)
{
    if (hz >= 1000u && hz % 1000u == 0u) {
        (void)snprintf(buf, n, "%u kHz", (unsigned)(hz / 1000u));
    } else {
        (void)snprintf(buf, n, "%u Hz", (unsigned)hz);
    }
}

void gpioview_width_text(uint32_t us, char *buf, size_t n)
{
    if (us >= 1000000u && us % 1000000u == 0u) {
        (void)snprintf(buf, n, "%u s", (unsigned)(us / 1000000u));
    } else if (us >= 1000u && us % 1000u == 0u) {
        (void)snprintf(buf, n, "%u ms", (unsigned)(us / 1000u));
    } else {
        (void)snprintf(buf, n, "%u us", (unsigned)us);
    }
}

void gpioview_state_text(const stark_lab_info_t *info, bool level_read, char *buf, size_t n)
{
    switch (info->mode) {
        case STARK_LAB_MODE_IN:
            (void)snprintf(buf, n, "IN %c", level_read ? 'H' : 'L');
            break;
        case STARK_LAB_MODE_OUT:
            (void)snprintf(buf, n, "OUT %c", info->level ? 'H' : 'L');
            break;
        case STARK_LAB_MODE_PWM:
            (void)snprintf(buf, n, "PWM");
            break;
        case STARK_LAB_MODE_HIZ:
        default:
            (void)snprintf(buf, n, "Hi-Z");
            break;
    }
}

void gpioview_home_label(int pin, const char *name, char *buf, size_t n)
{
    (void)snprintf(buf, n, "%d %s", pin, name != NULL ? name : "");
}

const char *gpioview_value(gpioview_row_t k, const gpioview_t *v, char *buf, size_t n)
{
    const gpioapp_cfg_t *c = v->cfg;
    switch (k) {
        case GPIOVIEW_ROW_MODE:
            return k_mode_text[c->mode < GPIOAPP_MODES ? c->mode : GPIOAPP_MODE_HIZ];
        case GPIOVIEW_ROW_PULL:
            return k_pull_text[c->pull <= STARK_LAB_PULL_DOWN ? c->pull : STARK_LAB_PULL_NONE];
        case GPIOVIEW_ROW_LEVEL:
            return c->level ? "High" : "Low";
        case GPIOVIEW_ROW_FREQ:
            gpioview_freq_text(c->hz, buf, n);
            return buf;
        case GPIOVIEW_ROW_DUTY:
            (void)snprintf(buf, n, "%u %%", (unsigned)c->duty_pct);
            return buf;
        case GPIOVIEW_ROW_WIDTH:
            gpioview_width_text(c->width_us, buf, n);
            return buf;
        case GPIOVIEW_ROW_PROBE:
            return v->probe != NULL && v->probe[0] != '\0' ? v->probe : NULL;
        case GPIOVIEW_ROW_READ:
            return v->level_read ? "H" : "L";
        case GPIOVIEW_ROW_MEASURE:
            return v->meas != NULL && v->meas[0] != '\0' ? v->meas : "50 kHz max";
        case GPIOVIEW_ROW_PAD:
            gpioview_state_text(v->info, v->level_read, buf, n);
            return buf;
        case GPIOVIEW_ROW_DRIVE:
        case GPIOVIEW_ROW_TOGGLE:
        case GPIOVIEW_ROW_PWM:
        case GPIOVIEW_ROW_PULSE:
        case GPIOVIEW_ROW_COUNT:
        default:
            return NULL;
    }
}

bool gpioview_adjust(gpioapp_cfg_t *c, gpioview_row_t k, int dir)
{
    switch (k) {
        case GPIOVIEW_ROW_MODE:
            if (dir > 0 && c->mode + 1 < GPIOAPP_MODES) {
                c->mode = (gpioapp_mode_t)(c->mode + 1);
                return true;
            }
            if (dir < 0 && c->mode > GPIOAPP_MODE_HIZ) {
                c->mode = (gpioapp_mode_t)(c->mode - 1);
                return true;
            }
            return false;
        case GPIOVIEW_ROW_PULL:
            if (dir > 0 && c->pull < STARK_LAB_PULL_DOWN) {
                c->pull = (stark_lab_pull_t)(c->pull + 1);
                return true;
            }
            if (dir < 0 && c->pull > STARK_LAB_PULL_NONE) {
                c->pull = (stark_lab_pull_t)(c->pull - 1);
                return true;
            }
            return false;
        case GPIOVIEW_ROW_LEVEL:
            if ((dir > 0 && !c->level) || (dir < 0 && c->level)) {
                c->level = !c->level;
                return true;
            }
            return false;
        case GPIOVIEW_ROW_FREQ: {
            uint32_t hz = stark_lab_pwm_step_hz(c->hz, dir);
            bool changed = hz != c->hz;
            c->hz = hz;
            return changed;
        }
        case GPIOVIEW_ROW_DUTY: {
            uint32_t duty = stark_lab_pwm_step_duty(c->duty_pct, dir);
            bool changed = duty != c->duty_pct;
            c->duty_pct = duty;
            return changed;
        }
        case GPIOVIEW_ROW_WIDTH: {
            uint32_t us = stark_lab_pulse_step(c->width_us, dir);
            bool changed = us != c->width_us;
            c->width_us = us;
            return changed;
        }
        case GPIOVIEW_ROW_PROBE:
        case GPIOVIEW_ROW_READ:
        case GPIOVIEW_ROW_MEASURE:
        case GPIOVIEW_ROW_DRIVE:
        case GPIOVIEW_ROW_TOGGLE:
        case GPIOVIEW_ROW_PWM:
        case GPIOVIEW_ROW_PULSE:
        case GPIOVIEW_ROW_PAD:
        case GPIOVIEW_ROW_COUNT:
        default:
            return false;
    }
}
