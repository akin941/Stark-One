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
 * GDO0. NOTE: STARK_ERR_NOT_SUPPORTED until STARK-0605: this task has no
 * transmit path, so nothing here can put a signal on GDO0.
 */
stark_err_t stark_subghz_transport_tx(const subghz_sym_t *s, size_t n, uint32_t timeout_ms);
