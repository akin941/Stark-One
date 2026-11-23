/*
 * stark_subghz_transport.h — the CC1101 module's wires (TASKS.md STARK-0601,
 * ADR-0022 §3): SPI bytes to the chip on the claimed slot, and the GDO data
 * lines as timed symbols (asynchronous serial mode). The only Sub-GHz code that
 * touches SPI or RMT. The emulator test image substitutes this component with
 * one built on the test-only fake CC1101.
 *
 * One caller at a time: the stark_subghz task.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"
#include "stark_module.h"

/* µs: > 0 high (carrier on), < 0 low; |v| 1..1 000 000. */
typedef int32_t subghz_sym_t;

/*
 * Attaches the claimed slot's chip on the shared SPI bus; the lease names the
 * lines (MOD_CS, GDO0 on MOD_IRQ0, GDO2 on MOD_IRQ1), so no pin number is
 * known here. STARK_ERR_BUSY while open, STARK_ERR_STATE for a stale lease.
 */
stark_err_t stark_subghz_transport_open(const stark_module_lease_t *lease);
void stark_subghz_transport_close(void);

/* One chip-select-framed SPI transfer; STARK_ERR_STATE when closed or the lease is stale. */
stark_err_t stark_subghz_transport_xfer(const uint8_t *tx, uint8_t *rx, size_t len);

/*
 * GDO2 → symbols. rx_take waits up to timeout_ms and returns how many it wrote (0: none),
 * at most 128 per call. Symbols arrive in frames that end at an idle gap; a frame that ends
 * high is followed by the gap as a 20 ms low, so consecutive frames never merge. rx_stop
 * is safe whether or not the receiver runs.
 */
stark_err_t stark_subghz_transport_rx_start(void);
size_t stark_subghz_transport_rx_take(subghz_sym_t *out, size_t max, uint32_t timeout_ms);
void stark_subghz_transport_rx_stop(void);

/*
 * GDO0 (STARK-0605): sends the symbols as the data line, high = carrier on (OOK), and returns
 * when the last one has left, STARK_ERR_TIMEOUT after timeout_ms. The line is low again when it
 * returns, on every outcome. The caller has already put the chip in TX (cc1101_tx_start()) and
 * puts it back in IDLE afterwards; this call moves no chip state. The transport enforces the
 * transmit limits itself, whatever the caller says: STARK_ERR_INVALID_ARG, before anything
 * moves, for a symbol list stark_subghz_tx_ok() refuses; STARK_ERR_STATE when closed or the
 * lease is stale.
 */
stark_err_t stark_subghz_transport_tx(const subghz_sym_t *s, size_t n, uint32_t timeout_ms);

/* The limits of one transmission (ADR-0022 §6): constants, never options. */
#define STARK_SUBGHZ_TX_MAX_SYMBOLS 2048u
#define STARK_SUBGHZ_TX_MAX_US      2000000u /* the sum of |symbol| */
#define STARK_SUBGHZ_TX_MARGIN_MS   100u     /* the timeout is the signal plus this, at most */

/*
 * True for 1..2048 symbols, each |v| 1..1 000 000 µs, their sum at most 2 s, and a timeout of
 * at least 1 ms and at most the signal plus STARK_SUBGHZ_TX_MARGIN_MS. Shared by the hardware
 * transport and the test one, so neither can send what the policy refuses.
 */
static inline bool stark_subghz_tx_ok(const subghz_sym_t *s, size_t n, uint32_t timeout_ms)
{
    if (s == NULL || n < 1u || n > STARK_SUBGHZ_TX_MAX_SYMBOLS) {
        return false;
    }
    uint32_t total = 0;
    for (size_t i = 0; i < n; i++) {
        int64_t v = s[i];
        uint32_t us = (uint32_t)(v < 0 ? -v : v);
        if (us < 1u || us > 1000000u) {
            return false;
        }
        total += us; /* at most 2048 x 1e6 < 2^32 */
        if (total > STARK_SUBGHZ_TX_MAX_US) {
            return false;
        }
    }
    return timeout_ms >= 1u && timeout_ms <= (total + 999u) / 1000u + STARK_SUBGHZ_TX_MARGIN_MS;
}
