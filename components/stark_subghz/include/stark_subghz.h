/*
 * stark_subghz.h — L3 Sub-GHz service (TASKS.md STARK-0601…0605, ADR-0022): a
 * CC1101 in a module slot, reached through stark_subghz_transport. The pure
 * cores (cc1101_regs.c, cc1101_drv.c, subghz_rssi.c, subghz_svc.c) make every
 * decision.
 *
 * Module state changes and measurements are published as STARK_EVT_SYSTEM /
 * STARK_SYS_SUBGHZ with system.arg = one of STARK_SUBGHZ_EVT_*. It receives (an RSSI
 * meter and a band scan, STARK-0602, raw capture, STARK-0603) and, under the fixed policy of
 * subghz_txpolicy.h and only on an explicit request, sends one saved capture once
 * (STARK-0605).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stark_err.h"
#include "subghz_raw.h"
#include "subghz_txpolicy.h"

#define STARK_SYS_SUBGHZ              7u

#define STARK_SUBGHZ_EVT_MODULE       1 /* stark_subghz_ready() changed */
#define STARK_SUBGHZ_EVT_RSSI         2 /* the meter has a new level */
#define STARK_SUBGHZ_EVT_SCAN_DONE    3 /* a sweep finished: the peak is in the status */
#define STARK_SUBGHZ_EVT_CAPTURE      4 /* a capture has more symbols (at most 10 a second) */
#define STARK_SUBGHZ_EVT_CAPTURE_DONE 5 /* a capture ended: finished, full or 2 s reached */
#define STARK_SUBGHZ_EVT_TX_DONE      6 /* a transmission went out; the radio is idle again */
#define STARK_SUBGHZ_EVT_TX_FAILED    7 /* it started and failed (counted in the budget) */
#define STARK_SUBGHZ_EVT_TX_REFUSED   8 /* the policy refused it on the transmit path */

/*
 * Starts the service task: it follows the module slots, claims one that declares
 * a radio, identifies the chip by its PARTNUM and VERSION, configures the 433 MHz
 * receive profile (left in IDLE), and publishes STARK_SUBGHZ_EVT_MODULE on every
 * change. Logs `subghz: no module`, `subghz: cc1101 version 0x<v>`, `subghz: not
 * a cc1101 (partnum 0x<p> version 0x<v>)` and `subghz: module lost`. Call after
 * stark_module_init(); never fails the boot for a missing module.
 */
stark_err_t stark_subghz_init(void);

/* True while a CC1101 is claimed, identified and answering. Any task. */
bool stark_subghz_ready(void);

typedef enum {
    STARK_SUBGHZ_IDLE, /* the radio is not receiving */
    STARK_SUBGHZ_METER,
    STARK_SUBGHZ_SCAN,
    STARK_SUBGHZ_CAPTURE,
    STARK_SUBGHZ_TX /* a transmission was requested (STARK-0605); see tx_done */
} stark_subghz_mode_t;

typedef struct {
    stark_subghz_mode_t mode; /* what was asked for last; IDLE after stop or a lost module */
    bool valid;               /* hz and dbm_x2 hold a measurement */
    bool scan_done;           /* a sweep finished (mode SCAN): hz and dbm_x2 are its peak */
    uint8_t percent;          /* sweep progress, 0..100 */
    uint32_t hz;              /* the tuned frequency (meter) or the peak channel (scan) */
    int16_t dbm_x2;           /* twice the dBm (subghz_rssi.h) */
    bool capture_done;        /* mode CAPTURE: it ended; the symbols are the caller's to use */
    uint16_t symbols;         /* mode CAPTURE: symbols kept so far */
    uint32_t span_us;         /* ... and their total duration */
    bool tx_done;             /* mode TX: the request was carried out or refused */
    uint8_t tx_verdict;       /* ... SUBGHZ_TX_OK, or why the transmit path refused it */
    stark_err_t tx_err;       /* ... STARK_OK, or why an attempt that started failed */
} stark_subghz_status_t;

/*
 * Receive-only measurements (ADR-0022 §5). The radio is in RX only between a
 * start and the stop; every call returns at once and the service task does the
 * work. STARK_ERR_STATE without a ready module, STARK_ERR_INVALID_ARG for a
 * frequency outside 433.05–434.79 MHz.
 *
 * meter_start tunes to hz and samples the RSSI every 50 ms; calling it again
 * retunes. STARK_SUBGHZ_EVT_RSSI follows each change of level. scan_start sweeps
 * the band once; STARK_SUBGHZ_EVT_SCAN_DONE follows, and the radio is back in
 * IDLE by then. stop returns the radio to IDLE (always safe, any state).
 */
stark_err_t stark_subghz_meter_start(uint32_t hz);
stark_err_t stark_subghz_scan_start(void);
void stark_subghz_stop(void);

/*
 * Raw capture (STARK-0603, ADR-0022 §5). Tunes hz in the preset of mod (OOK or 2-FSK at
 * 4800 Bd), receives, and keeps the symbols GDO2 produces in buf (cap symbols, at most
 * 2048) by the rules of subghz_cap_feed(): leading silence dropped, same-sign symbols
 * merged, stopping at 2048 symbols or when the kept symbols span 2 s. Receive only.
 * Returns at once; STARK_SUBGHZ_EVT_CAPTURE follows as symbols arrive and
 * STARK_SUBGHZ_EVT_CAPTURE_DONE when it ends, with the radio back in IDLE.
 * STARK_ERR_STATE without a ready module, STARK_ERR_INVALID_ARG for a frequency
 * outside the band, an unknown modulation or a missing buffer. Calling it again
 * starts a new capture.
 *
 * The service writes into buf only between this call and the end of the capture
 * (the status says capture_done) or the next stark_subghz_stop()/start of another
 * measurement; after any of those the buffer is the caller's again.
 * capture_finish asks the capture to end now; the symbols so far are kept.
 */
stark_err_t stark_subghz_capture_start(subghz_mod_t mod, uint32_t hz, subghz_sym_t *buf,
                                       size_t cap);
void stark_subghz_capture_finish(void);

/*
 * Replay of one saved capture (STARK-0605, ADR-0022 §6). Nothing here transmits by itself:
 * stark_subghz_tx_start() is called once per explicit user confirmation and sends once.
 *
 * tx_check asks the policy what sending raw would be (the duration, the duty budget left, the
 * off-time still to wait, and the fingerprint of the symbols) without sending anything; a
 * refusal is logged `subghz: tx refused: <reason>`. raw must be what subghz_raw_parser_finish()
 * made from a saved file. name (the file, for the log) is copied.
 *
 * tx_start checks everything again, including that the symbols still hash to `hash` (the
 * value tx_check gave, so what is sent is what was confirmed), and on SUBGHZ_TX_OK queues
 * exactly one transmission: the service task tunes OOK at PATABLE 0x60, sends the symbols,
 * returns the radio to IDLE, and publishes STARK_SUBGHZ_EVT_TX_DONE or _TX_FAILED (the status
 * then has mode TX and tx_done). The policy is checked a third time on the transmit path, and
 * the band and the power table are read back from the chip before STX. A refusal is returned,
 * logged, and sends nothing; info (optional) says why (the off-time still to wait, ...). raw's
 * symbols must stay valid until the event; stop() drops a request that has not started, and waits
 * for one that has (at most 2.1 s).
 */
subghz_tx_verdict_t stark_subghz_tx_check(const subghz_raw_t *raw, const char *name,
                                          subghz_tx_info_t *info);
subghz_tx_verdict_t stark_subghz_tx_start(const subghz_raw_t *raw, const char *name, uint32_t hash,
                                          subghz_tx_info_t *info);

/* A copy of the latest measurement. Any task. */
void stark_subghz_status(stark_subghz_status_t *out);
