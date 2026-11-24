/*
 * lab_uart.h — the UART tool's data rules (TASKS.md STARK-0705, ADR-0023 §7): the baud and format
 * lists, the byte ring with its loss count, the loopback pattern and its check, and the wire-time
 * arithmetic the loopback test's window comes from. Pure: no lock, no allocation, no ESP-IDF
 * header, every loop bounded by its argument. The text helpers the app uses (hex and ASCII rows,
 * the send file, the log names, the CRC-32) are the stark_lab_uart_*() functions of stark_lab.h,
 * defined in lab_uart.c. The session that drives the transport is lab_uart_svc.c; stark_lab.c
 * binds that to the log and the event bus.
 *
 * Nothing here knows a pin: the pins are stark_board's and the service's (lab_uart_svc.h).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"
#include "stark_lab.h"
#include "stark_lab_transport.h"

#define LAB_UART_BAUDS   STARK_LAB_UART_BAUDS
#define LAB_UART_FORMATS STARK_LAB_UART_FMTS

_Static_assert(STARK_LAB_UART_FMTS == STARK_LAB_UART_FORMATS, "the transport's formats are ours");

typedef struct {
    const char *name; /* "8N1" */
    uint8_t data_bits;
    char parity; /* 'N', 'E' or 'O' */
    uint8_t stop_bits;
} lab_uart_fmt_t;

/* The index of a baud in the list, or SIZE_MAX. */
size_t lab_uart_baud_index(uint32_t baud);

/* The format list by index, NULL past the end. */
const lab_uart_fmt_t *lab_uart_fmt(size_t index);

/* Bits on the wire per character: start, data, parity if any, stop. 0 for an unknown format. */
uint32_t lab_uart_bits_per_char(size_t fmt);

/* Milliseconds `n` characters take on the wire, rounded up; 0 when the baud is 0 or the format is
 * unknown. */
uint32_t lab_uart_wire_ms(size_t n, uint32_t baud, size_t fmt);

/* How long the loopback test listens: twice the wire time of the pattern plus 20 ms. */
uint32_t lab_uart_loop_window_ms(uint32_t baud, size_t fmt);

/* ---- the byte ring ---------------------------------------------------------------------------
 * Fixed capacity (the caller's buffer); a full ring overwrites its oldest byte and counts it in
 * `lost`, so a loss is never silent. */
typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t head;    /* index of the oldest byte */
    size_t count;   /* bytes held, at most cap */
    uint32_t lost;  /* bytes overwritten */
    uint32_t total; /* bytes ever pushed */
} lab_uart_ring_t;

void lab_uart_ring_init(lab_uart_ring_t *r, uint8_t *buf, size_t cap);
void lab_uart_ring_clear(lab_uart_ring_t *r); /* empty, counters to zero */
void lab_uart_ring_push(lab_uart_ring_t *r, const uint8_t *data, size_t n);
/* Copies up to `max` bytes starting `skip` bytes after the oldest; returns how many. */
size_t lab_uart_ring_peek(const lab_uart_ring_t *r, size_t skip, uint8_t *out, size_t max);

/* ---- the loopback pattern -------------------------------------------------------------------- */

/* The 64 bytes 00 01 02 ... 3f (STARK_LAB_UART_LOOP_LEN). */
void lab_uart_loop_pattern(uint8_t out[STARK_LAB_UART_LOOP_LEN]);

/*
 * Judges a loopback: `rx` holds the `n_rx` bytes that came back for the `sent` bytes of the
 * pattern, `framing`, `parity` and `overrun` what the receiver counted during the test. A result is
 * OK only when every byte matches and no error was counted: bytes that match with errors counted
 * are LINE_ERRORS, anything differing or missing is MISMATCH with its first offset, and nothing at
 * all is NO_DATA. Bytes beyond `sent` count as errors.
 */
void lab_uart_loop_check(const uint8_t *rx, size_t n_rx, size_t sent, uint32_t framing,
                         uint32_t parity, uint32_t overrun, stark_lab_loop_t *out);
