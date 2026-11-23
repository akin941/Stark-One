/*
 * stark_subghz.h — L3 Sub-GHz service (TASKS.md STARK-0601…0605, ADR-0022): a
 * CC1101 in a module slot, reached through stark_subghz_transport. The pure
 * cores (cc1101_regs.c, cc1101_drv.c, subghz_rssi.c, subghz_svc.c) make every
 * decision.
 *
 * Module state changes and measurements are published as STARK_EVT_SYSTEM /
 * STARK_SYS_SUBGHZ with system.arg = one of STARK_SUBGHZ_EVT_*. This version
 * receives only: an RSSI meter and a band scan (STARK-0602) and raw capture
 * (STARK-0603). It never transmits.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stark_err.h"
#include "subghz_raw.h"

#define STARK_SYS_SUBGHZ              7u

#define STARK_SUBGHZ_EVT_MODULE       1 /* stark_subghz_ready() changed */
#define STARK_SUBGHZ_EVT_RSSI         2 /* the meter has a new level */
#define STARK_SUBGHZ_EVT_SCAN_DONE    3 /* a sweep finished: the peak is in the status */
#define STARK_SUBGHZ_EVT_CAPTURE      4 /* a capture has more symbols (at most 10 a second) */
#define STARK_SUBGHZ_EVT_CAPTURE_DONE 5 /* a capture ended: finished, full or 2 s reached */

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
    STARK_SUBGHZ_CAPTURE
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

/* A copy of the latest measurement. Any task. */
void stark_subghz_status(stark_subghz_status_t *out);
