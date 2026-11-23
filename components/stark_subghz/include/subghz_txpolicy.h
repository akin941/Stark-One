/*
 * subghz_txpolicy.h — the Sub-GHz transmit policy (TASKS.md STARK-0605, ADR-0022 §6,
 * docs/SECURITY_SCOPE.md §4). Pure: no I/O, no ESP-IDF, an injected clock. Every limit is a
 * constant in this file: there is no setter, no Kconfig option and no way for the UI to widen
 * one. The service (subghz_svc.c) asks the policy when a transmission is requested, asks it
 * again on the transmit path, and records every attempt in it.
 *
 * Limits: one transmission is in 433.05–434.79 MHz, at most 2048 symbols and at most 2 s; in
 * any rolling 3600 s at most 360 s are transmitted; after a transmission of d the next one
 * waits 9 d. The ledger is RAM and a boot starts it with the longest possible off-time
 * already running (SUBGHZ_TX_BOOT_LOCK_MS), so a reboot never reopens the duty window more
 * than a fresh start would (ADR-0022 addendum, STARK-0605).
 *
 * Time is a free-running uint32 in ms and may wrap: every comparison is a difference, and
 * subghz_txpolicy_tick() (called every service round) drops entries long before they could
 * look young again.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_subghz_transport.h" /* subghz_sym_t and the transport's own limits */
#include "subghz_raw.h"
#include "subghz_rssi.h" /* the band's edges */

#define SUBGHZ_TX_MAX_SYMBOLS  SUBGHZ_RAW_MAX_SYMBOLS /* 2048 */
#define SUBGHZ_TX_MAX_US       SUBGHZ_RAW_MAX_US      /* 2 s of signal */
#define SUBGHZ_TX_WINDOW_MS    3600000u               /* the rolling window ... */
#define SUBGHZ_TX_BUDGET_MS    360000u                /* ... and what it may hold: 10 % */
#define SUBGHZ_TX_OFF_FACTOR   9u                     /* off-time = 9 x the transmission */
#define SUBGHZ_TX_BOOT_LOCK_MS (SUBGHZ_TX_OFF_FACTOR * 2000u)
#define SUBGHZ_TX_LEDGER       192u /* entries: exact up to this many in a window (180 x 2 s fill it) */

_Static_assert(SUBGHZ_TX_MAX_SYMBOLS == STARK_SUBGHZ_TX_MAX_SYMBOLS,
               "the policy and the transport agree on the symbol limit");
_Static_assert(SUBGHZ_TX_MAX_US == STARK_SUBGHZ_TX_MAX_US,
               "the policy and the transport agree on the duration limit");

typedef enum {
    SUBGHZ_TX_OK = 0,
    SUBGHZ_TX_REFUSE_BAND,      /* the file's frequency is outside 433.05–434.79 MHz */
    SUBGHZ_TX_REFUSE_LENGTH,    /* no symbols, or more than 2048 */
    SUBGHZ_TX_REFUSE_DURATION,  /* the signal is longer than 2 s */
    SUBGHZ_TX_REFUSE_BUDGET,    /* it would exceed 360 s in the rolling 3600 s */
    SUBGHZ_TX_REFUSE_OFFTIME,   /* 9 x the last transmission has not passed */
    SUBGHZ_TX_REFUSE_FILE,      /* not a valid OOK capture, or it changed since it was confirmed */
    SUBGHZ_TX_REFUSE_NO_MODULE, /* no ready radio */
    SUBGHZ_TX_REFUSE_BUSY,      /* a transmission is already pending */
    SUBGHZ_TX_VERDICT_COUNT
} subghz_tx_verdict_t;

/* "band", "length", "duration", "budget", "offtime", "file", "no module", "busy"; "ok"; "?". */
const char *subghz_tx_verdict_name(subghz_tx_verdict_t v);

typedef struct {
    uint32_t end_ms; /* when it ended (or was last known to be on air) */
    uint32_t dur_ms; /* what it is charged: the planned signal, rounded up to a ms */
} subghz_tx_entry_t;

typedef struct {
    subghz_tx_entry_t e[SUBGHZ_TX_LEDGER];
    uint8_t n;
    bool have_last; /* the off-time of the last transmission may still be running */
    uint32_t last_end_ms;
    uint32_t last_dur_ms;
} subghz_txpolicy_t;

/* What one transmission would be, from its description (not from the caller's say-so). */
typedef struct {
    uint32_t freq_hz;
    uint32_t symbols;
    uint32_t span_us; /* the sum of |symbol| */
} subghz_tx_plan_t;

typedef struct {
    uint32_t duration_ms;    /* the plan, rounded up to a ms: what it is charged */
    uint32_t budget_left_ms; /* of the 360 s, before this transmission */
    uint32_t wait_ms;        /* the off-time still to pass (0: none) */
    uint32_t hash;           /* subghz_raw_hash() of the symbols; filled by the service */
} subghz_tx_info_t;

/* Starts the ledger empty with the boot lock running from now_ms. */
void subghz_txpolicy_init(subghz_txpolicy_t *p, uint32_t now_ms);

/* Drops what has aged out. Called every service round; any other call prunes first as well. */
void subghz_txpolicy_tick(subghz_txpolicy_t *p, uint32_t now_ms);

/* 360 s minus what the window holds. */
uint32_t subghz_txpolicy_budget_left_ms(subghz_txpolicy_t *p, uint32_t now_ms);

/*
 * SUBGHZ_TX_OK, or the first limit the plan breaks, checked in this order: band, length,
 * duration, budget, off-time. info (optional) is filled whatever the verdict. Changes nothing
 * but the pruning of expired entries.
 */
subghz_tx_verdict_t subghz_txpolicy_check(subghz_txpolicy_t *p, uint32_t now_ms,
                                          const subghz_tx_plan_t *plan, subghz_tx_info_t *info);

/*
 * An attempt that began at start_ms ended at end_ms with dur_ms charged (a failed one is
 * charged in full: how long the carrier was up is not known). The ledger keeps the later of
 * end_ms and start_ms + dur_ms, so the time on air is never under-counted.
 */
void subghz_txpolicy_record(subghz_txpolicy_t *p, uint32_t start_ms, uint32_t end_ms,
                            uint32_t dur_ms);

/* The span in ms, rounded up (and at least 1). */
uint32_t subghz_tx_ms(uint32_t span_us);
