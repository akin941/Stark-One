/*
 * ui_port_gpio.c — the host UI port's lab service (ui_port_gpio.h): stark_lab.h over lab_pins.c
 * and the fake pads. It mirrors stark_lab.c (components/stark_lab), which adds the mutex, the real
 * delays and the log lines; the decisions are the same code.
 */
#include "ui_port_gpio.h"
#include <string.h>
#include "fake_lab.h"
#include "lab_pins.h"
#include "stark_event.h"
#include "stark_log.h"
#include "ui_port_module.h"

/* HARDWARE.md §2's map: the lab pins and slot 0's three lines. */
static const stark_board_pins_t k_map = {
    .lab_uart_tx = 38,
    .lab_uart_rx = 39,
    .module = {{.cs = 40, .irq0 = 41, .irq1 = 42, .en = -1, .eeprom_addr = 0x50},
               {.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1, .eeprom_addr = 0x51}},
};

static lab_pins_t s_lab;
static const stark_board_pins_t *s_map = &k_map;
static unsigned s_cancels;
static unsigned s_meas_cancels;
static uint64_t s_now_us;

static size_t io_list(void *ctx, stark_module_entry_t *out, size_t max)
{
    (void)ctx;
    return stark_module_list(out, max);
}

static stark_err_t io_pad_in(void *ctx, int pin, uint8_t pull)
{
    (void)ctx;
    return fake_lab_pad_in(pin, pull);
}

static stark_err_t io_pad_out(void *ctx, int pin, bool level)
{
    (void)ctx;
    return fake_lab_pad_out(pin, level);
}

static stark_err_t io_pad_read(void *ctx, int pin, bool *level)
{
    (void)ctx;
    return fake_lab_pad_read(pin, level);
}

static stark_err_t io_pad_hiz(void *ctx, int pin)
{
    (void)ctx;
    return fake_lab_pad_hiz(pin);
}

static stark_err_t io_pwm_start(void *ctx, int pin, uint32_t hz, uint8_t duty_pct)
{
    (void)ctx;
    return fake_lab_pwm_start(pin, hz, duty_pct);
}

static stark_err_t io_pulse(void *ctx, int pin, bool level, uint32_t us)
{
    (void)ctx;
    return fake_lab_pulse(pin, level, us);
}

static stark_err_t io_edges(void *ctx, int pin, uint32_t window_us, uint32_t *ts, size_t cap,
                            size_t *n, uint32_t *seen, bool *first_level)
{
    (void)ctx;
    stark_err_t err = fake_lab_pad_edges(pin, window_us, ts, cap, n, seen, first_level);
    s_now_us += window_us; /* the capture took its window */
    fake_lab_time_us(s_now_us);
    return err;
}

static uint32_t io_tick_hz(void *ctx)
{
    (void)ctx;
    return FAKE_LAB_TICK_HZ;
}

static void io_delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    s_now_us += us;
    fake_lab_time_us(s_now_us);
}

static void io_log(void *ctx, lab_log_t what, int a, int b)
{
    (void)ctx;
    if (what == LAB_LOG_WITHDRAWN) {
        STARK_LOGW("lab", "pin %d withdrawn (slot %d)", a, b);
    } else {
        STARK_LOGW("lab", "contention pin %d", a);
    }
}

static void build(void)
{
    const lab_io_t io = {.list = io_list,
                         .pad_in = io_pad_in,
                         .pad_out = io_pad_out,
                         .pad_read = io_pad_read,
                         .pad_hiz = io_pad_hiz,
                         .pwm_start = io_pwm_start,
                         .pwm_stop = io_pad_hiz, /* the fake pad leaves PWM with its release */
                         .pulse = io_pulse,
                         .edges = io_edges,
                         .tick_hz = io_tick_hz,
                         .delay_us = io_delay_us,
                         .log = io_log};
    lab_pins_init(&s_lab, &io, s_map);
}

static void publish(void)
{
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_LAB;
    e.system.arg = STARK_LAB_EVT_PINS;
    (void)stark_event_publish(&e);
}

static void leave(void)
{
    if (lab_pins_take_changed(&s_lab)) {
        publish();
    }
}

void ui_port_gpio_reset(void)
{
    ui_port_gpio_reset_with(NULL);
}

void ui_port_gpio_reset_with(const stark_board_pins_t *map)
{
    s_map = map != NULL ? map : &k_map;
    stark_module_entry_t t[STARK_MODULE_ENTRIES];
    memset(t, 0, sizeof(t));
    t[0] =
        (stark_module_entry_t){.kind = STARK_MODULE_SLOT, .state = STARK_MODULE_ABSENT, .slot = 0};
    t[1] =
        (stark_module_entry_t){.kind = STARK_MODULE_SLOT, .state = STARK_MODULE_ABSENT, .slot = 1};
    ui_port_module_reset();
    ui_port_module_set(t, STARK_MODULE_ENTRIES);
    fake_lab_reset();
    s_cancels = 0;
    s_meas_cancels = 0;
    s_now_us = 0;
    build();
}

void ui_port_gpio_set_slot0(stark_module_state_t state)
{
    stark_module_entry_t t[STARK_MODULE_ENTRIES];
    size_t n = stark_module_list(t, STARK_MODULE_ENTRIES);
    t[0].state = state;
    ui_port_module_set(t, n);
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_MODULE;
    e.system.arg = 0;
    (void)stark_event_publish(&e);
}

unsigned ui_port_gpio_cancels(void)
{
    return s_cancels;
}

unsigned ui_port_gpio_measure_cancels(void)
{
    return s_meas_cancels;
}

/* ---- stark_lab.h ---------------------------------------------------------------------------- */

size_t stark_lab_pins(stark_lab_pin_t *out, size_t max)
{
    size_t n = lab_pins_list(&s_lab, out, max);
    leave();
    return n;
}

stark_err_t stark_lab_acquire(int pin, const char *owner)
{
    stark_err_t err = lab_pins_acquire(&s_lab, pin, owner);
    leave();
    return err;
}

void stark_lab_release(int pin)
{
    lab_pins_release(&s_lab, pin);
    STARK_LOGI("lab", "released pin %d", pin);
}

void stark_lab_release_all(void)
{
    lab_pins_release_all(&s_lab);
    STARK_LOGI("lab", "released all");
}

stark_err_t stark_lab_arm(int pin)
{
    stark_err_t err = lab_pins_arm(&s_lab, pin);
    leave();
    if (err == STARK_OK) {
        STARK_LOGI("lab", "armed pin %d", pin);
    }
    return err;
}

stark_err_t stark_lab_input(int pin, stark_lab_pull_t pull)
{
    stark_err_t err = lab_pins_input(&s_lab, pin, pull);
    leave();
    return err;
}

stark_err_t stark_lab_hiz(int pin)
{
    stark_err_t err = lab_pins_hiz(&s_lab, pin);
    leave();
    return err;
}

stark_err_t stark_lab_read(int pin, bool *level)
{
    stark_err_t err = lab_pins_read(&s_lab, pin, level);
    leave();
    return err;
}

stark_err_t stark_lab_state(int pin, stark_lab_pinstate_t *st)
{
    stark_err_t err = lab_pins_state(&s_lab, pin, st);
    leave();
    return err;
}

stark_err_t stark_lab_write(int pin, bool level)
{
    stark_err_t err = lab_pins_write(&s_lab, pin, level);
    leave();
    return err;
}

stark_err_t stark_lab_toggle(int pin)
{
    stark_err_t err = lab_pins_toggle(&s_lab, pin);
    leave();
    return err;
}

stark_err_t stark_lab_info(int pin, stark_lab_info_t *out)
{
    stark_err_t err = lab_pins_info(&s_lab, pin, out);
    leave();
    return err;
}

stark_err_t stark_lab_pwm_start(int pin, uint32_t hz, uint32_t duty_pct)
{
    stark_err_t err = lab_pins_pwm_start(&s_lab, pin, hz, duty_pct);
    leave();
    if (err == STARK_OK) {
        STARK_LOGI("lab", "pwm pin %d %u Hz %u %%", pin, (unsigned)hz, (unsigned)duty_pct);
    }
    return err;
}

void stark_lab_pwm_stop(void)
{
    lab_pins_pwm_stop(&s_lab);
    leave();
    STARK_LOGI("lab", "pwm stopped");
}

stark_err_t stark_lab_pulse(int pin, bool level, uint32_t us)
{
    stark_err_t err = lab_pins_pulse(&s_lab, pin, level, us);
    leave();
    if (err == STARK_OK) {
        STARK_LOGI("lab", "pulse pin %d %s %u us", pin, level ? "high" : "low", (unsigned)us);
    }
    return err;
}

void stark_lab_pulse_cancel(void)
{
    s_cancels++;
}

void stark_lab_refresh(void)
{
    if (lab_pins_refresh(&s_lab)) {
        publish();
    }
}

stark_err_t stark_lab_measure(int pin, uint32_t *ring, size_t cap, stark_lab_meas_t *out)
{
    stark_err_t err = lab_pins_measure(&s_lab, pin, ring, cap, out);
    leave();
    if (err == STARK_OK) {
        char text[40];
        stark_lab_meas_log_text(out, text, sizeof text);
        STARK_LOGI("lab", "measure %s", text);
    }
    return err;
}

void stark_lab_measure_cancel(void)
{
    s_meas_cancels++;
}
