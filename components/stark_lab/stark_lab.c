/*
 * stark_lab.c — the lab service's target port (stark_lab.h): binds the pure pad decisions
 * (lab_pins.c) to stark_module's table, stark_lab_transport, the log and the event bus. One
 * mutex serialises every call; the operations on it are short (the longest is the state
 * probe, 4 ms of busy waiting). Nothing is allocated after init and no task is created: a
 * call runs on its caller's task. Target-only.
 */
#include "stark_lab.h"
#include <stdio.h>
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lab_pins.h"
#include "stark_event.h"
#include "stark_lab_transport.h"
#include "stark_log.h"
#include "stark_module.h"

static lab_pins_t s_pins;
static bool s_inited;
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;

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
                         .delay_us = io_delay_us,
                         .log = io_log};
    lab_pins_init(&s_pins, &io, stark_board_pins());
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
    }
}

void stark_lab_release_all(void)
{
    if (enter()) {
        lab_pins_release_all(&s_pins);
        leave();
    }
}

stark_err_t stark_lab_arm(int pin)
{
    if (!enter()) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = lab_pins_arm(&s_pins, pin);
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

void stark_lab_refresh(void)
{
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
