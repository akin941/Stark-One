/*
 * lab_spi.h — the SPI probe's rules (TASKS.md STARK-0706, ADR-0023 §6): which slot may be probed
 * and why not, the request and its fingerprint, the single-use arm tied to exactly what the dialog
 * showed, the claim-use-release of the lease, and the log text. Pure, over an injected module
 * table, lease and bus: no lock, no allocation, no ESP-IDF header, no loop longer than 32 bytes.
 * stark_lab.c binds it to stark_module, stark_lab_transport, the log and the event bus; the host
 * tests bind it to the real module core and the test-only SPI devices (test/fake/fake_lab.c).
 *
 * The rule that protects the radio: a slot is probed only when its descriptor declares MOD_SPI and
 * no function bit (stark_lab_spi_entry_ok(), shared with the transport's guard). A Sub-GHz, NFC, IR
 * or unknown-bit slot is a driver's, and a raw write to a CC1101 could set PATABLE or strobe STX
 * and walk around the transmit policy (ADR-0022 §6).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "module_desc.h"
#include "stark_board.h"
#include "stark_lab.h"
#include "stark_lab_transport.h"
#include "stark_module.h"

#define LAB_SPI_ARM_MS  30000u /* a request is stale after this long */
#define LAB_SPI_OWNER   "spiprobe"
#define LAB_SPI_LOG_MAX 288u /* the longest log line: 32 bytes each way, in hex */
#define LAB_SPI_HEX_MAX (3u * STARK_LAB_SPI_MAX)
#define LAB_SPI_FP_SEED 2166136261u /* 32-bit FNV-1a */

_Static_assert(STARK_LAB_SPI_MAX == STARK_LAB_SPI_BYTES_MAX &&
                   STARK_LAB_SPI_CLOCKS == STARK_LAB_SPI_CLOCK_COUNT &&
                   STARK_LAB_SPI_NAME_MAX == MODULE_NAME_MAX &&
                   STARK_LAB_SPI_SLOTS == STARK_MODULE_SLOTS,
               "the app's limits are the transport's and the module table's");

typedef struct {
    void *ctx;
    size_t (*list)(void *ctx, stark_module_entry_t *out, size_t max);
    /* Claims the slot's chip select for LAB_SPI_OWNER (stark_module_claim() with
     * STARK_CAP_MOD_SPI). */
    stark_err_t (*claim)(void *ctx, uint8_t slot, stark_module_lease_t *out);
    void (*release)(void *ctx, const stark_module_lease_t *l);
    stark_err_t (*xfer)(void *ctx, const stark_module_lease_t *l, uint32_t hz, uint8_t mode,
                        const uint8_t *tx, uint8_t *rx, size_t n);
    uint32_t (*now_ms)(void *ctx);
    void (*log)(void *ctx, bool warn, const char *text); /* the text after `lab: ` */
    void (*done)(void *ctx);                             /* STARK_LAB_EVT_SPI_DONE */
} lab_spi_io_t;

typedef struct {
    lab_spi_io_t io;
    bool cs[STARK_MODULE_SLOTS]; /* the board wires MOD_CS for the slot */
    bool armed;
    uint32_t fp;
    uint32_t armed_at_ms;
} lab_spi_t;

/* The wiring comes from the pin map (stark_board.h), never from here: slot 1 has no MOD_CS. */
void lab_spi_init(lab_spi_t *c, const lab_spi_io_t *io, const stark_board_pins_t *pins);

/*
 * Judges one table entry (NULL: the slot is not in the table) for a slot whose chip select is wired
 * or not. Pure. `why` and `name` are filled as stark_lab_spi_slot_t documents; the reasons, in the
 * order they are decided: no chip select, no module, generic module, unsupported module, conflict,
 * owned by <Sub-GHz|NFC|IR|unknown>, no SPI declared. `eligible` equals
 * stark_lab_spi_entry_ok() with the chip select wired.
 */
void lab_spi_judge(const stark_module_entry_t *e, bool cs_wired, uint8_t slot,
                   stark_lab_spi_slot_t *out);

/* Both slots as they are now (at most STARK_LAB_SPI_SLOTS); drives nothing. */
size_t lab_spi_slots(lab_spi_t *c, stark_lab_spi_slot_t *out, size_t max);

stark_err_t lab_spi_request(lab_spi_t *c, uint8_t slot, uint32_t hz, uint8_t mode,
                            const uint8_t *tx, size_t n, stark_lab_spi_req_t *out);

/*
 * The single-use arm: the request is consumed first, then the arguments, the age, the slot and the
 * fingerprint are checked, the slot is judged again, the lease is claimed, the slot is judged
 * again (a module cannot change under a lease: a replacement revokes it), the transaction is made
 * and the lease is released. See stark_lab_spi_send() for the errors.
 */
stark_err_t lab_spi_send(lab_spi_t *c, const stark_lab_spi_req_t *req, uint8_t *rx);

void lab_spi_cancel(lab_spi_t *c);

/* True after a request that no send, cancel or new request has consumed (stale ones count). */
bool lab_spi_armed(const lab_spi_t *c);

/*
 * The fingerprint of what the dialog showed: 32-bit FNV-1a over 'S', the slot, the clock (four
 * bytes, low first), the mode, the count, the bytes, and the module's identity (id, two bytes low
 * first; rev; declared, four bytes low first), so a different module in the slot is a different
 * transaction. `e` may be NULL (the identity is then zeros).
 */
uint32_t lab_spi_fp(uint8_t slot, uint32_t hz, uint8_t mode, const uint8_t *tx, size_t n,
                    const stark_module_entry_t *e);
