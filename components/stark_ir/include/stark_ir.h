/*
 * stark_ir.h — L3 infrared service (TASKS.md STARK-0303/0304, ADR-0019).
 *
 * Receive-first: the RMT RX channel on IR_RX is created at boot but only
 * listens while an app has turned it on. Each received frame is converted
 * to timings, decoded (ir_codec.h), kept in a small ring and announced as
 * STARK_EVT_SYSTEM / STARK_SYS_IR with system.arg = STARK_IR_EVT_RX.
 */
#pragma once

#include <stdint.h>
#include "ir_types.h"
#include "stark_err.h"

/* STARK_EVT_SYSTEM system.id (ARCHITECTURE §6.3 keeps the id table). */
#define STARK_SYS_IR 4u

enum {
    STARK_IR_EVT_RX = 1,        /* a capture is ready: stark_ir_take() */
    STARK_IR_EVT_TX_DONE = 2,   /* STARK-0304 */
    STARK_IR_EVT_TX_FAILED = 3, /* STARK-0304 */
};

typedef struct {
    uint32_t frames;    /* captures received */
    uint32_t decoded;   /* ... that matched a protocol */
    uint32_t unknown;   /* ... that did not (kept as raw timings) */
    uint32_t overflows; /* frames longer than IR_MAX_DURATIONS (dropped) */
    uint32_t dropped;   /* captures pushed out of the full ring */
    uint32_t tx;        /* transmissions completed (STARK-0304) */
} stark_ir_stats_t;

/* Creates the RMT channel(s) and the IR task; RX stays off. On a board
 * without IR pins logs `ir: not fitted` and returns STARK_OK, after which
 * every other call is STARK_ERR_NOT_SUPPORTED. STARK_ERR_STATE if already
 * initialised. Logs `ir: init rx=<gpio> tx=<gpio>`. */
stark_err_t stark_ir_init(void);

/* Listening on / off (`ir: rx on` / `ir: rx off`). Any task. */
stark_err_t stark_ir_rx_start(void);
stark_err_t stark_ir_rx_stop(void);

/*
 * Starts a transmission of s (and `repeats` more frames at the protocol's
 * period) and returns at once; STARK_SYS_IR TX_DONE or TX_FAILED follows.
 * Call only after the user confirmed (SECURITY_SCOPE §4.2). The gate
 * (ir_tx_guard.h) refuses with STARK_ERR_INVALID_ARG anything outside its
 * limits — at most 1 s of signal, 3 repeats, a 30–60 kHz carrier — and
 * with STARK_ERR_BUSY a transmission still running or one started less than
 * CONFIG_STARK_IR_TX_MIN_INTERVAL_MS after the previous one ended. Each one
 * is logged (`ir: tx <proto> addr= cmd= carrier= dur= repeats=`, then
 * `ir: tx done` / `ir: tx failed: <err>`). STARK_ERR_NOT_SUPPORTED without
 * IR. (STARK-0304)
 */
stark_err_t stark_ir_transmit(const ir_signal_t *s, uint8_t repeats);

/* The oldest capture; STARK_ERR_NOT_FOUND when there is none. Any task. */
stark_err_t stark_ir_take(ir_signal_t *out);

stark_err_t stark_ir_stats(stark_ir_stats_t *out);
