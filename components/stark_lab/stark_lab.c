/*
 * stark_lab.c — the lab service's target port (stark_lab.h): binds the pure pad decisions
 * (lab_pins.c) to stark_module's table, stark_lab_transport, the log and the event bus. One
 * mutex serialises every call; the operations on it are short (the longest is the state
 * probe, 4 ms of busy waiting) except a pulse, which holds it for its width (up to 1 s) on the
 * app's worker and can be cut short by stark_lab_pulse_cancel(), which takes no lock. Nothing is
 * allocated after init and no task is created: a call runs on its caller's task. Target-only.
 */
#include "stark_lab.h"
#include <stdio.h>
#include <string.h>
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lab_i2c.h"
#include "lab_pins.h"
#include "stark_caps.h"
#include "stark_event.h"
#include "stark_lab_transport.h"
#include "stark_log.h"
#include "stark_module.h"

static lab_pins_t s_pins;
static lab_i2c_t s_i2c;
static bool s_inited;
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t
    s_i2c_lock_buf; /* the I2C bus has its own: a dump must not hold up a pin */
static SemaphoreHandle_t s_i2c_lock;

static size_t io_list(void *ctx, stark_module_entry_t *out, size_t max)
{
    (void)ctx;
    return stark_module_list(out, max);
}

static stark_err_t io_pad_in(void *ctx, int pin, uint8_t pull)
{
    (void)ctx;
    return stark_lab_transport_pad_in(pin, pull);
}

static stark_err_t io_pad_out(void *ctx, int pin, bool level)
{
    (void)ctx;
    return stark_lab_transport_pad_out(pin, level);
}

static stark_err_t io_pad_read(void *ctx, int pin, bool *level)
{
    (void)ctx;
    return stark_lab_transport_pad_read(pin, level);
}

static stark_err_t io_pad_hiz(void *ctx, int pin)
{
    (void)ctx;
    return stark_lab_transport_pad_hiz(pin);
}

static stark_err_t io_pwm_start(void *ctx, int pin, uint32_t hz, uint8_t duty_pct)
{
    (void)ctx;
    return stark_lab_transport_pwm_start(pin, hz, duty_pct);
}

static stark_err_t io_pwm_stop(void *ctx, int pin)
{
    (void)ctx;
    return stark_lab_transport_pwm_stop(pin);
}

static stark_err_t io_pulse(void *ctx, int pin, bool level, uint32_t us)
{
    (void)ctx;
    return stark_lab_transport_pulse(pin, level, us);
}

static stark_err_t io_edges(void *ctx, int pin, uint32_t window_us, uint32_t *ts, size_t cap,
                            size_t *n, uint32_t *seen, bool *first_level)
{
    (void)ctx;
    return stark_lab_transport_pad_edges(pin, window_us, ts, cap, n, seen, first_level);
}

static uint32_t io_tick_hz(void *ctx)
{
    (void)ctx;
    return stark_lab_transport_tick_hz();
}

static void io_delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    /* NOTE: a busy wait, not vTaskDelay: the read-back and the pull probe want 1 and 2 ms,
     * and a tick is longer. At most 4 ms in one call, on the caller's task. */
    esp_rom_delay_us(us);
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

static stark_err_t io_i2c_probe(void *ctx, uint8_t addr, uint32_t timeout_ms)
{
    (void)ctx;
    return stark_lab_transport_i2c_probe(addr, timeout_ms);
}

static stark_err_t io_i2c_xfer(void *ctx, uint8_t addr, uint32_t hz, const uint8_t *w, size_t wn,
                               uint8_t *r, size_t rn, uint32_t timeout_ms)
{
    (void)ctx;
    return stark_lab_transport_i2c_xfer(addr, hz, w, wn, r, rn, timeout_ms);
}

static bool io_nfc_confirmed(void *ctx)
{
    (void)ctx;
    return (stark_module_caps() & STARK_CAP_NFC) != 0u;
}

static uint32_t io_now_ms(void *ctx)
{
    (void)ctx;
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* `lab: pins <the pins on offer now>`, one number each. */
static void log_pins(void)
{
    stark_lab_pin_t list[STARK_BOARD_LAB_PINS_MAX];
    size_t n = lab_pins_list(&s_pins, list, STARK_BOARD_LAB_PINS_MAX);
    char text[8 * STARK_BOARD_LAB_PINS_MAX + 1];
    size_t used = 0;
    text[0] = '\0';
    for (size_t i = 0; i < n && i < STARK_BOARD_LAB_PINS_MAX; i++) {
        if (list[i].offered) {
            used += (size_t)snprintf(text + used, sizeof(text) - used, " %d", list[i].pin);
        }
    }
    STARK_LOGI("lab", "pins%s", text);
}

static void publish_pins(void)
{
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_LAB;
    e.system.arg = STARK_LAB_EVT_PINS;
    (void)stark_event_publish(&e);
}

/* Takes the lock; false before init. */
static bool enter(void)
{
    if (!s_inited) {
        return false;
    }
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    return true;
}

/* Leaves the lock and tells the bus if a withdrawal happened inside the call. */
static void leave(void)
{
    bool changed = lab_pins_take_changed(&s_pins);
    (void)xSemaphoreGive(s_lock);
    if (changed) {
        publish_pins();
    }
}

stark_err_t stark_lab_init(void)
{
    if (s_inited) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = stark_lab_transport_open();
    if (err != STARK_OK) {
        return err;
    }
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    const lab_io_t io = {.list = io_list,
                         .pad_in = io_pad_in,
                         .pad_out = io_pad_out,
                         .pad_read = io_pad_read,
                         .pad_hiz = io_pad_hiz,
                         .pwm_start = io_pwm_start,
                         .pwm_stop = io_pwm_stop,
                         .pulse = io_pulse,
                         .edges = io_edges,
                         .tick_hz = io_tick_hz,
                         .delay_us = io_delay_us,
                         .log = io_log};
    lab_pins_init(&s_pins, &io, stark_board_pins());
    const lab_i2c_io_t i2c_io = {.probe = io_i2c_probe,
                                 .xfer = io_i2c_xfer,
                                 .nfc_confirmed = io_nfc_confirmed,
                                 .now_ms = io_now_ms};
    lab_i2c_init(&s_i2c, &i2c_io, stark_board_pins());
    s_i2c_lock = xSemaphoreCreateMutexStatic(&s_i2c_lock_buf);
    s_inited = true;
    log_pins();
    return STARK_OK;
}

size_t stark_lab_pins(stark_lab_pin_t *out, size_t max)
{
    if (!enter()) {
        return 0;
    }
    size_t n = lab_pins_list(&s_pins, out, max);
    leave();
    return n;
}

stark_err_t stark_lab_acquire(int pin, const char *owner)
{
    if (!enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_pins_acquire(&s_pins, pin, owner);
    leave();
    return err;
}

void stark_lab_release(int pin)
{
    if (enter()) {
        lab_pins_release(&s_pins, pin);
        leave();
        STARK_LOGI("lab", "released pin %d", pin);
    }
}

void stark_lab_release_all(void)
{
    if (enter()) {
        lab_pins_release_all(&s_pins);
        leave();
        STARK_LOGI("lab", "released all");
    }
}

stark_err_t stark_lab_arm(int pin)
{
    if (!enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_pins_arm(&s_pins, pin);
    leave();
    if (err == STARK_OK) {
        STARK_LOGI("lab", "armed pin %d", pin);
    }
    return err;
}

stark_err_t stark_lab_hiz(int pin)
{
    if (!enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_pins_hiz(&s_pins, pin);
    leave();
    return err;
}

stark_err_t stark_lab_input(int pin, stark_lab_pull_t pull)
{
    if (!enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_pins_input(&s_pins, pin, pull);
    leave();
    return err;
}

stark_err_t stark_lab_read(int pin, bool *level)
{
    if (!enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_pins_read(&s_pins, pin, level);
    leave();
    return err;
}

stark_err_t stark_lab_state(int pin, stark_lab_pinstate_t *st)
{
    if (!enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_pins_state(&s_pins, pin, st);
    leave();
    return err;
}

stark_err_t stark_lab_write(int pin, bool level)
{
    if (!enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_pins_write(&s_pins, pin, level);
    leave();
    return err;
}

stark_err_t stark_lab_info(int pin, stark_lab_info_t *out)
{
    if (!enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_pins_info(&s_pins, pin, out);
    leave();
    return err;
}

stark_err_t stark_lab_toggle(int pin)
{
    if (!enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_pins_toggle(&s_pins, pin);
    leave();
    return err;
}

stark_err_t stark_lab_pwm_start(int pin, uint32_t hz, uint32_t duty_pct)
{
    if (!enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_pins_pwm_start(&s_pins, pin, hz, duty_pct);
    leave();
    if (err == STARK_OK) {
        STARK_LOGI("lab", "pwm pin %d %u Hz %u %%", pin, (unsigned)hz, (unsigned)duty_pct);
    }
    return err;
}

void stark_lab_pwm_stop(void)
{
    if (enter()) {
        lab_pins_pwm_stop(&s_pins);
        leave();
        STARK_LOGI("lab", "pwm stopped");
    }
}

stark_err_t stark_lab_pulse(int pin, bool level, uint32_t us)
{
    if (!enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_pins_pulse(&s_pins, pin, level, us);
    leave();
    if (err == STARK_OK) {
        STARK_LOGI("lab", "pulse pin %d %s %u us", pin, level ? "high" : "low", (unsigned)us);
    }
    return err;
}

void stark_lab_pulse_cancel(void)
{
    stark_lab_transport_pulse_cancel();
}

stark_err_t stark_lab_measure(int pin, uint32_t *ring, size_t cap, stark_lab_meas_t *out)
{
    if (!enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_pins_measure(&s_pins, pin, ring, cap, out);
    leave();
    if (err == STARK_OK) {
        char text[40];
        stark_lab_meas_log_text(out, text, sizeof text);
        STARK_LOGI("lab", "measure %s", text);
        stark_event_t e = {.type = STARK_EVT_SYSTEM};
        e.system.id = STARK_SYS_LAB;
        e.system.arg = STARK_LAB_EVT_MEASURE_DONE;
        (void)stark_event_publish(&e);
    }
    return err;
}

void stark_lab_measure_cancel(void)
{
    stark_lab_transport_edges_cancel();
}

void stark_lab_refresh(void)
{
    stark_lab_transport_pulse_cancel(); /* a pulse in flight must not hold the lock past a hot-plug
                                         */
    stark_lab_transport_edges_cancel(); /* nor a measurement, which can wait 4 s */
    if (!enter()) {
        return;
    }
    bool changed = lab_pins_refresh(&s_pins);
    if (changed) {
        log_pins();
    }
    (void)xSemaphoreGive(s_lock);
    if (changed) {
        publish_pins();
    }
}

/* ---- the shared I2C bus (STARK-0704) ------------------------------------------------------- */

/* Takes the I2C lock; false before init. */
static bool i2c_enter(void)
{
    if (!s_inited) {
        return false;
    }
    (void)xSemaphoreTake(s_i2c_lock, portMAX_DELAY);
    return true;
}

static void i2c_leave(void)
{
    (void)xSemaphoreGive(s_i2c_lock);
}

/* `0x23 0x3c ...`: the first 16 addresses, then `+<n>`; "none" for an empty list. */
static void scan_text(const stark_lab_scan_t *r, char *buf, size_t sz)
{
    size_t used = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < r->n && i < 16u; i++) {
        used += (size_t)snprintf(buf + used, sz - used, "%s0x%02x", i > 0u ? " " : "", r->addr[i]);
    }
    if (r->n > 16u) {
        used += (size_t)snprintf(buf + used, sz - used, " +%u", (unsigned)(r->n - 16u));
    }
    if (r->n == 0u) {
        (void)snprintf(buf, sz, "none");
    }
}

stark_err_t stark_lab_i2c_scan(stark_lab_scan_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!i2c_enter()) {
        memset(out, 0, sizeof(*out));
        out->status = STARK_ERR_STATE;
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_i2c_scan(&s_i2c, out);
    i2c_leave();
    char text[16u * 5u + 16u];
    scan_text(out, text, sizeof text);
    if (err == STARK_OK) {
        if (out->errors > 0u) {
            STARK_LOGI("lab", "i2c scan found %s (%u errors)", text, (unsigned)out->errors);
        } else {
            STARK_LOGI("lab", "i2c scan found %s", text);
        }
    } else {
        STARK_LOGW("lab", "i2c scan bus error found %s", text);
    }
    return err;
}

stark_err_t stark_lab_i2c_read(uint8_t addr, uint16_t reg, bool reg16, uint8_t *buf, size_t n,
                               uint32_t hz)
{
    if (!i2c_enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_i2c_read(&s_i2c, addr, reg, reg16, buf, n, hz);
    const char *why = err == STARK_ERR_NOT_SUPPORTED
                          ? lab_i2c_refusal_text(lab_i2c_refusal(&s_i2c, addr, reg16 ? 2u : 1u, 1u))
                          : "";
    i2c_leave();
    char data[3u * STARK_LAB_I2C_R_MAX];
    if (err == STARK_OK) {
        (void)lab_i2c_hex(buf, n, data, sizeof data);
        STARK_LOGI("lab", "i2c read 0x%02x reg 0x%0*x %s", addr, reg16 ? 4 : 2, reg, data);
    } else if (err == STARK_ERR_NOT_SUPPORTED) {
        STARK_LOGW("lab", "i2c read refused: %s", why);
    } else {
        STARK_LOGW("lab", "i2c read 0x%02x reg 0x%0*x %s", addr, reg16 ? 4 : 2, reg,
                   stark_err_str(err));
    }
    return err;
}

stark_err_t stark_lab_i2c_dump(uint8_t addr, uint8_t *buf256, uint32_t hz)
{
    if (!i2c_enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_i2c_dump(&s_i2c, addr, buf256, hz);
    const char *why = err == STARK_ERR_NOT_SUPPORTED
                          ? lab_i2c_refusal_text(lab_i2c_refusal(&s_i2c, addr, 1u, 1u))
                          : "";
    i2c_leave();
    if (err == STARK_ERR_NOT_SUPPORTED) {
        STARK_LOGW("lab", "i2c dump refused: %s", why);
    } else {
        STARK_LOGI("lab", "i2c dump 0x%02x %s", addr, err == STARK_OK ? "ok" : stark_err_str(err));
    }
    return err;
}

stark_err_t stark_lab_i2c_write(uint8_t addr, uint16_t reg, bool reg16, const uint8_t *v, size_t n,
                                uint32_t hz)
{
    if (!i2c_enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_i2c_write(&s_i2c, addr, reg, reg16, v, n, hz);
    const char *why =
        err == STARK_ERR_NOT_SUPPORTED
            ? lab_i2c_refusal_text(lab_i2c_refusal(&s_i2c, addr, (reg16 ? 2u : 1u) + n, 0u))
            : "";
    i2c_leave();
    if (err == STARK_ERR_NOT_SUPPORTED) {
        STARK_LOGW("lab", "i2c write refused: %s", why);
    } else {
        char data[3u * LAB_I2C_WRITE_MAX];
        (void)lab_i2c_hex(v, v != NULL && n <= LAB_I2C_WRITE_MAX ? n : 0u, data, sizeof data);
        STARK_LOGI("lab", "i2c write 0x%02x reg 0x%0*x %s %s", addr, reg16 ? 4 : 2, reg, data,
                   err == STARK_OK ? "ok" : stark_err_str(err));
    }
    return err;
}

stark_err_t stark_lab_i2c_arm(uint32_t fingerprint)
{
    if (!i2c_enter()) {
        return STARK_ERR_STATE;
    }
    lab_i2c_arm(&s_i2c, fingerprint);
    i2c_leave();
    return STARK_OK;
}

void stark_lab_i2c_disarm(void)
{
    if (i2c_enter()) {
        lab_i2c_disarm(&s_i2c);
        i2c_leave();
    }
}

const char *stark_lab_i2c_refusal(uint8_t addr, bool reg16, bool write)
{
    if (!i2c_enter()) {
        return NULL;
    }
    lab_i2c_refusal_t why = lab_i2c_refusal(&s_i2c, addr, reg16 ? 2u : 1u, write ? 0u : 1u);
    i2c_leave();
    return why == LAB_I2C_REFUSE_NONE ? NULL : lab_i2c_refusal_text(why);
}
