/*
 * lab_uart_svc.h — the UART tool's session (TASKS.md STARK-0705, ADR-0023 §7): which pins it holds,
 * the receive ring and counters, the send and loopback rules, and the auto-baud capture, over an
 * injected I/O table. Pure: no lock of its own (the table's data lock is a hook), no ESP-IDF
 * header; stark_lab.c binds it to stark_lab_transport, the pin hold, the log and the event bus, and
 * the host tests bind it to the test-only UART peer (test/fake/fake_lab.c) and to scripted faults.
 *
 * One session at a time. It holds both lab pins from open to close (so the GPIO app cannot), starts
 * receive-only (the TX pad untouched), switches TX on only after the app's confirmation, and gives
 * everything back, driver included, on close and on every failure: no call leaves a driver
 * installed or a pin held after it returned an error that ended the session. The only allocations
 * are the 4 kB ring and the driver's buffer while open, and the 2 kB edge buffer of one auto-baud.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lab_autobaud.h"
#include "lab_uart.h"
#include "stark_board.h"
#include "stark_lab.h"
#include "stark_lab_transport.h"

#define LAB_UARTSVC_RXBUF       2048u    /* the driver's receive buffer: 22 ms of 921 600 baud */
#define LAB_UARTSVC_EDGES       512u     /* stamps one auto-baud keeps (2 kB) */
#define LAB_UARTSVC_AUTOBAUD_US 2000000u /* an auto-baud listens up to 2 s */
#define LAB_UARTSVC_SLICE_MS    10u      /* the longest single wait of a loopback listen */
#define LAB_UARTSVC_NOTE_MS     100u     /* events at most this often */
#define LAB_UARTSVC_DRAIN       8u       /* reads that empty the driver before a test */

typedef enum {
    LAB_UART_LOG_OPEN, /* a = baud, b = format index */
    LAB_UART_LOG_TX_ON,
    LAB_UART_LOG_CLOSED,
    LAB_UART_LOG_SEND,     /* a = bytes */
    LAB_UART_LOG_LOOP,     /* p = const stark_lab_loop_t * */
    LAB_UART_LOG_AUTOBAUD, /* p = const stark_lab_autobaud_t * */
    LAB_UART_LOG_FAILED    /* a = stark_err_t: the session ended */
} lab_uart_log_t;

typedef enum { LAB_UART_NOTE_DATA, LAB_UART_NOTE_ERROR } lab_uart_note_t;

typedef struct {
    void *ctx;
    /* the pin hold (owner "uart"): lab_pins_acquire / release / arm */
    stark_err_t (*pin_acquire)(void *ctx, int pin);
    void (*pin_release)(void *ctx, int pin);
    stark_err_t (*pin_arm)(void *ctx, int pin);
    stark_err_t (*pad_in)(void *ctx, int pin); /* input, no pull, of a held pin */
    stark_err_t (*pad_edges)(void *ctx, int pin, uint32_t window_us, uint32_t *ts, size_t cap,
                             size_t *n, uint32_t *seen, bool *first_level);
    uint32_t (*tick_hz)(void *ctx);
    /* the transport's UART group */
    stark_err_t (*open)(void *ctx, uint32_t baud, uint8_t fmt, size_t rx_buf, bool tx);
    stark_err_t (*set_baud)(void *ctx, uint32_t baud);
    int (*read)(void *ctx, uint8_t *buf, size_t max, uint32_t timeout_ms,
                stark_lab_uart_err_t *errs);
    int (*write)(void *ctx, const uint8_t *buf, size_t n, uint32_t timeout_ms);
    void (*close)(void *ctx);
    uint32_t (*now_ms)(void *ctx);
    bool (*cancelled)(void *ctx);   /* a loopback test or auto-baud was asked to stop */
    void (*data_lock)(void *ctx);   /* around the ring and counters; may be NULL */
    void (*data_unlock)(void *ctx); /* may be NULL */
    void (*log)(void *ctx, lab_uart_log_t what, const void *p, uint32_t a, uint32_t b);
    void (*note)(void *ctx, lab_uart_note_t what); /* publish an event; at most every 100 ms */
} lab_uart_io_t;

typedef struct {
    lab_uart_io_t io;
    int tx_pin, rx_pin; /* from the board's pin map */
    bool open;
    bool tx;
    size_t baud_i, fmt_i;
    uint8_t *ring_buf;
    lab_uart_ring_t ring;
    uint32_t framing, parity, overrun;
    bool pending_data, pending_err;
    bool note_valid;
    uint32_t last_note_ms;
} lab_uart_ses_t;

void lab_uartsvc_init(lab_uart_ses_t *s, const lab_uart_io_t *io, const stark_board_pins_t *pins);

/* The calls of stark_lab.h, one for one (see there for the contract). */
stark_err_t lab_uartsvc_open(lab_uart_ses_t *s, size_t baud_index, size_t fmt_index);
stark_err_t lab_uartsvc_tx_enable(lab_uart_ses_t *s);
stark_err_t lab_uartsvc_set_format(lab_uart_ses_t *s, size_t baud_index, size_t fmt_index);
stark_err_t lab_uartsvc_poll(lab_uart_ses_t *s, uint8_t *chunk, size_t max, uint32_t timeout_ms,
                             size_t *got);
size_t lab_uartsvc_ring_read(lab_uart_ses_t *s, size_t skip, uint8_t *out, size_t max);
void lab_uartsvc_stats(lab_uart_ses_t *s, stark_lab_uart_stats_t *out);
void lab_uartsvc_clear(lab_uart_ses_t *s);
stark_err_t lab_uartsvc_send(lab_uart_ses_t *s, const uint8_t *buf, size_t n);
stark_err_t lab_uartsvc_loop_test(lab_uart_ses_t *s, stark_lab_loop_t *out);
stark_err_t lab_uartsvc_autobaud(lab_uart_ses_t *s, stark_lab_autobaud_t *out);
void lab_uartsvc_close(lab_uart_ses_t *s);
