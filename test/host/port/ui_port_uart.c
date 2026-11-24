/*
 * ui_port_uart.c — the host UI port's lab service, UART group (ui_port_uart.h): stark_lab.h's
 * stark_lab_uart_*() over lab_uart_svc.c, the pin hold of lab_pins.c, the host shim of the
 * transport and the fake peer. It mirrors stark_lab.c (components/stark_lab), which adds the locks
 * and the real clock; the decisions, the log lines and the events are the same.
 */
#include "ui_port_uart.h"
#include <stdio.h>
#include <string.h>
#include "fake_lab.h"
#include "lab_pins.h"
#include "lab_transport_uart_host.h"
#include "lab_uart_svc.h"
#include "stark_event.h"
#include "stark_log.h"

/* HARDWARE.md §2's map as far as the UART reads it: the two lab pins. */
static const stark_board_pins_t k_map = {
    .lab_uart_tx = 38,
    .lab_uart_rx = 39,
    .module = {{.cs = 40, .irq0 = 41, .irq1 = 42, .en = -1, .eeprom_addr = 0x50},
               {.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1, .eeprom_addr = 0x51}},
};

const stark_board_pins_t *stark_board_pins(void)
{
    return &k_map;
}

static lab_pins_t s_pins;
static lab_uart_ses_t s_ses;
static volatile bool s_cancel;
static unsigned s_data_events;
static unsigned s_error_events;

static size_t pio_list(void *ctx, stark_module_entry_t *out, size_t max)
{
    (void)ctx;
    (void)out;
    (void)max;
    return 0;
}

static stark_err_t pio_pad_in(void *ctx, int pin, uint8_t pull)
{
    (void)ctx;
    return fake_lab_pad_in(pin, pull);
}

static stark_err_t pio_pad_out(void *ctx, int pin, bool level)
{
    (void)ctx;
    return fake_lab_pad_out(pin, level);
}

static stark_err_t pio_pad_read(void *ctx, int pin, bool *level)
{
    (void)ctx;
    return fake_lab_pad_read(pin, level);
}

static stark_err_t pio_pad_hiz(void *ctx, int pin)
{
    (void)ctx;
    return fake_lab_pad_hiz(pin);
}

static void pio_delay(void *ctx, uint32_t us)
{
    (void)ctx;
    (void)us;
}

static void pio_log(void *ctx, lab_log_t what, int a, int b)
{
    (void)ctx;
    (void)what;
    (void)a;
    (void)b;
}

static stark_err_t uio_pin_acquire(void *ctx, int pin)
{
    (void)ctx;
    return lab_pins_acquire(&s_pins, pin, "uart");
}

static void uio_pin_release(void *ctx, int pin)
{
    (void)ctx;
    lab_pins_release(&s_pins, pin);
    STARK_LOGI("lab", "released pin %d", pin);
}

static stark_err_t uio_pin_arm(void *ctx, int pin)
{
    (void)ctx;
    return lab_pins_arm(&s_pins, pin);
}

static stark_err_t uio_pad_in(void *ctx, int pin)
{
    (void)ctx;
    return lab_pins_input(&s_pins, pin, STARK_LAB_PULL_NONE);
}

static stark_err_t uio_pad_edges(void *ctx, int pin, uint32_t window_us, uint32_t *ts, size_t cap,
                                 size_t *n, uint32_t *seen, bool *first_level)
{
    (void)ctx;
    return fake_lab_pad_edges(pin, window_us, ts, cap, n, seen, first_level);
}

static uint32_t uio_tick_hz(void *ctx)
{
    (void)ctx;
    return FAKE_LAB_TICK_HZ;
}

static stark_err_t uio_open(void *ctx, uint32_t baud, uint8_t fmt, size_t rx_buf, bool tx)
{
    (void)ctx;
    return stark_lab_transport_uart_open(baud, fmt, rx_buf, tx);
}

static stark_err_t uio_set_baud(void *ctx, uint32_t baud)
{
    (void)ctx;
    return stark_lab_transport_uart_set_baud(baud);
}

static int uio_read(void *ctx, uint8_t *buf, size_t max, uint32_t timeout_ms,
                    stark_lab_uart_err_t *errs)
{
    (void)ctx;
    return stark_lab_transport_uart_read(buf, max, timeout_ms, errs);
}

static int uio_write(void *ctx, const uint8_t *buf, size_t n, uint32_t timeout_ms)
{
    (void)ctx;
    return stark_lab_transport_uart_write(buf, n, timeout_ms);
}

static void uio_close(void *ctx)
{
    (void)ctx;
    stark_lab_transport_uart_close();
}

static uint32_t uio_now_ms(void *ctx)
{
    (void)ctx;
    return (uint32_t)(lab_uart_host_time_us() / 1000u);
}

static bool uio_cancelled(void *ctx)
{
    (void)ctx;
    return s_cancel;
}

static void uio_log(void *ctx, lab_uart_log_t what, const void *p, uint32_t a, uint32_t b)
{
    (void)ctx;
    char text[48];
    switch (what) {
        case LAB_UART_LOG_OPEN:
            STARK_LOGI("lab", "uart open %u %s", (unsigned)a, stark_lab_uart_fmt_name(b));
            break;
        case LAB_UART_LOG_TX_ON:
            STARK_LOGI("lab", "uart tx on");
            break;
        case LAB_UART_LOG_CLOSED:
            STARK_LOGI("lab", "uart closed");
            break;
        case LAB_UART_LOG_SEND:
            STARK_LOGI("lab", "uart send %u bytes", (unsigned)a);
            break;
        case LAB_UART_LOG_LOOP:
            stark_lab_loop_log_text(p, text, sizeof text);
            STARK_LOGI("lab", "uart loop %s", text);
            break;
        case LAB_UART_LOG_AUTOBAUD:
            STARK_LOGI("lab", "autobaud %s", stark_lab_autobaud_text(p, text, sizeof text));
            break;
        case LAB_UART_LOG_FAILED:
        default:
            STARK_LOGW("lab", "uart failed: %s", stark_err_str((stark_err_t)a));
            break;
    }
}

static void uio_note(void *ctx, lab_uart_note_t what)
{
    (void)ctx;
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_LAB;
    if (what == LAB_UART_NOTE_ERROR) {
        e.system.arg = STARK_LAB_EVT_UART_ERROR;
        s_error_events++;
    } else {
        e.system.arg = STARK_LAB_EVT_UART_DATA;
        s_data_events++;
    }
    (void)stark_event_publish(&e);
}

void ui_port_uart_reset(void)
{
    fake_lab_reset();
    lab_uart_host_reset();
    s_cancel = false;
    s_data_events = 0;
    s_error_events = 0;
    const lab_io_t pio = {.list = pio_list,
                          .pad_in = pio_pad_in,
                          .pad_out = pio_pad_out,
                          .pad_read = pio_pad_read,
                          .pad_hiz = pio_pad_hiz,
                          .delay_us = pio_delay,
                          .log = pio_log};
    lab_pins_init(&s_pins, &pio, &k_map);
    const lab_uart_io_t uio = {.pin_acquire = uio_pin_acquire,
                               .pin_release = uio_pin_release,
                               .pin_arm = uio_pin_arm,
                               .pad_in = uio_pad_in,
                               .pad_edges = uio_pad_edges,
                               .tick_hz = uio_tick_hz,
                               .open = uio_open,
                               .set_baud = uio_set_baud,
                               .read = uio_read,
                               .write = uio_write,
                               .close = uio_close,
                               .now_ms = uio_now_ms,
                               .cancelled = uio_cancelled,
                               .log = uio_log,
                               .note = uio_note};
    lab_uartsvc_init(&s_ses, &uio, &k_map);
    fake_lab_uart_pins(k_map.lab_uart_tx, k_map.lab_uart_rx);
}

bool ui_port_uart_held(int pin)
{
    stark_lab_pin_t list[8];
    size_t n = lab_pins_list(&s_pins, list, 8);
    for (size_t i = 0; i < n; i++) {
        if (list[i].pin == pin) {
            return list[i].held;
        }
    }
    return false;
}

stark_err_t ui_port_uart_take(int pin)
{
    return lab_pins_acquire(&s_pins, pin, "gpio");
}

void ui_port_uart_give(int pin)
{
    lab_pins_release(&s_pins, pin);
}

unsigned ui_port_uart_opens(void)
{
    return lab_uart_host_opens();
}

unsigned ui_port_uart_closes(void)
{
    return lab_uart_host_closes();
}

void ui_port_uart_fail(int call, int after)
{
    lab_uart_host_fail((lab_uart_host_call_t)call, after);
}

unsigned ui_port_uart_data_events(void)
{
    return s_data_events;
}

unsigned ui_port_uart_error_events(void)
{
    return s_error_events;
}

uint64_t ui_port_uart_time_us(void)
{
    return lab_uart_host_time_us();
}

void ui_port_uart_advance_us(uint64_t us)
{
    lab_uart_host_set_time_us(lab_uart_host_time_us() + us);
}

/* ---- stark_lab.h ---------------------------------------------------------------------------- */

stark_err_t stark_lab_uart_open(size_t baud_index, size_t fmt_index)
{
    s_cancel = false;
    return lab_uartsvc_open(&s_ses, baud_index, fmt_index);
}

stark_err_t stark_lab_uart_tx_enable(void)
{
    return lab_uartsvc_tx_enable(&s_ses);
}

stark_err_t stark_lab_uart_set_format(size_t baud_index, size_t fmt_index)
{
    return lab_uartsvc_set_format(&s_ses, baud_index, fmt_index);
}

stark_err_t stark_lab_uart_poll(uint8_t *chunk, size_t max, uint32_t timeout_ms, size_t *got)
{
    return lab_uartsvc_poll(&s_ses, chunk, max, timeout_ms, got);
}

size_t stark_lab_uart_ring_read(size_t skip, uint8_t *out, size_t max)
{
    return lab_uartsvc_ring_read(&s_ses, skip, out, max);
}

void stark_lab_uart_stats(stark_lab_uart_stats_t *out)
{
    lab_uartsvc_stats(&s_ses, out);
}

void stark_lab_uart_clear(void)
{
    lab_uartsvc_clear(&s_ses);
}

stark_err_t stark_lab_uart_send(const uint8_t *buf, size_t n)
{
    return lab_uartsvc_send(&s_ses, buf, n);
}

stark_err_t stark_lab_uart_loop_test(stark_lab_loop_t *out)
{
    s_cancel = false;
    return lab_uartsvc_loop_test(&s_ses, out);
}

stark_err_t stark_lab_uart_autobaud(stark_lab_autobaud_t *out)
{
    s_cancel = false;
    return lab_uartsvc_autobaud(&s_ses, out);
}

void stark_lab_uart_cancel(void)
{
    s_cancel = true;
}

void stark_lab_uart_close(void)
{
    lab_uartsvc_close(&s_ses);
}
