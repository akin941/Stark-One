/*
 * subghz_svc.h — the module service's decisions (TASKS.md STARK-0601,
 * ADR-0022 §2): find a slot that declares a radio, claim it, identify the chip
 * by its own answer, configure the 433 MHz profile, and keep checking. Pure,
 * over an injected I/O table; stark_subghz.c binds it to stark_module,
 * stark_subghz_transport and FreeRTOS, and the host tests bind it to the real
 * module core and the fake CC1101.
 *
 * A descriptor only declares. The service reports READY only after
 * cc1101_detect() has succeeded, and the composition root turns that into the
 * capability (main/module_bridge.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cc1101_drv.h"
#include "stark_module.h"
#include "stark_subghz.h"
#include "subghz_raw.h"
#include "subghz_rssi.h"
#include "subghz_txpolicy.h"

#define SUBGHZ_WIRING          (STARK_CAP_SUBGHZ | STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ)
#define SUBGHZ_DEFAULT_HZ      433920000u
#define SUBGHZ_DEFAULT_BAUD    SUBGHZ_RAW_RATE_DEFAULT
#define SUBGHZ_RETRY_MS        2000u /* before a rejected module is identified again */
#define SUBGHZ_RETRY_MAX_MS    8000u /* ... doubling while the same rejection repeats, up to this */
#define SUBGHZ_MISS_LIMIT      2u    /* failed checks in a row that drop a ready module */
#define SUBGHZ_OP_MISS_LIMIT   2u    /* failed measurements in a row that drop a ready module */
#define SUBGHZ_IDLE_MS         250u  /* the longest wait the step asks for */
#define SUBGHZ_LOG_DELTA_X2    6     /* the meter logs a level when it moved this much (3 dB) */
#define SUBGHZ_TAKE_MAX        128u  /* symbols taken from the transport per round */
#define SUBGHZ_CAPTURE_POLL_MS 20u   /* the longest a round waits for symbols */
#define SUBGHZ_CAPTURE_NOTE_MS 100u  /* progress events are at least this far apart */

typedef enum {
    SUBGHZ_LOG_NO_MODULE,  /* nothing declares a radio */
    SUBGHZ_LOG_VERSION,    /* a = VERSION */
    SUBGHZ_LOG_NOT_CC1101, /* a = PARTNUM, b = VERSION */
    SUBGHZ_LOG_LOST,       /* a ready module went away */
    SUBGHZ_LOG_ERROR,      /* a = stark_err_t of the step that failed */
    SUBGHZ_LOG_METER,      /* a = Hz: the meter tuned */
    SUBGHZ_LOG_RSSI,       /* a = twice the dBm (as unsigned) */
    SUBGHZ_LOG_PEAK, /* a = Hz, b = twice the dBm (as unsigned): a sweep's strongest channel */
    SUBGHZ_LOG_IDLE, /* the radio was returned to IDLE on request */
    SUBGHZ_LOG_CAPTURE_START, /* a = Hz, b = subghz_mod_t */
    SUBGHZ_LOG_CAPTURE_DONE   /* a = symbols kept, b = subghz_raw_hash() of them */
} subghz_log_t;

/* What the transmit path reports (STARK-0605): one line each in the log. */
typedef struct {
    subghz_tx_verdict_t verdict; /* not OK: refused, and nothing was sent */
    stark_err_t err;             /* verdict OK: STARK_OK it went out, else why the attempt failed */
    const char *name;            /* the file */
    uint32_t freq_hz;
    uint32_t symbols;
    uint32_t ms;
} subghz_tx_log_t;

typedef struct {
    size_t (*list)(void *ctx, stark_module_entry_t *out, size_t max);
    stark_err_t (*claim)(void *ctx, uint8_t slot, stark_module_lease_t *out);
    void (*release)(void *ctx, const stark_module_lease_t *lease);
    bool (*lease_valid)(void *ctx, const stark_module_lease_t *lease);
    stark_err_t (*open)(void *ctx, const stark_module_lease_t *lease);
    void (*close)(void *ctx);
    stark_err_t (*xfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);
    stark_err_t (*rx_start)(void *ctx);
    size_t (*rx_take)(void *ctx, subghz_sym_t *out, size_t max, uint32_t timeout_ms);
    void (*rx_stop)(void *ctx);
    uint32_t (*now_ms)(void *ctx);
    void (*delay_ms)(void *ctx, uint32_t ms);
    void (*log)(void *ctx, subghz_log_t what, unsigned a, unsigned b);
    void (*changed)(void *ctx, bool ready); /* after every change of readiness */
    void (*notify)(void *ctx, int evt);     /* a STARK_SUBGHZ_EVT_* of a measurement or a send */
    /* GDO0: the symbols, high = carrier on; blocks until sent or timeout_ms (STARK-0605). */
    stark_err_t (*tx)(void *ctx, const subghz_sym_t *s, size_t n, uint32_t timeout_ms);
    void (*log_tx)(void *ctx, const subghz_tx_log_t *what);
    void *ctx;
} subghz_svc_io_t;

typedef struct {
    subghz_svc_io_t io;
    cc1101_t drv;
    stark_module_lease_t lease;
    bool claimed;     /* lease and transport are held */
    bool ready;       /* identified, configured, confirmed to the world */
    bool none_logged; /* "no module" is said once */
    bool have_retry;  /* retry_at is set */
    uint32_t retry_at;
    uint32_t retry_ms; /* the current wait: it doubles while the same rejection repeats */
    bool have_fail;    /* the last rejection, so a repeat is not logged again */
    subghz_log_t fail_what;
    unsigned fail_a;
    unsigned fail_b;
    uint8_t misses;
    /* ---- measurements (STARK-0602) ---- */
    stark_subghz_status_t st;
    bool rx_on;        /* the chip may be receiving because of a measurement */
    bool idle_log;     /* `idle` is said once the radio is back in IDLE */
    bool retune;       /* the meter has to (re)tune before sampling */
    bool scan_active;  /* a sweep is running */
    bool have_logged;  /* logged_x2 is set */
    int16_t logged_x2; /* the level last logged, so the log is not a stream */
    uint32_t meter_hz;
    uint32_t next_ms;  /* when the meter samples next */
    uint8_t op_misses; /* failed measurements in a row */
    subghz_scan_t scan;
    /* ---- capture (STARK-0603) ---- */
    subghz_cap_t cap;
    subghz_mod_t cap_mod;
    uint32_t cap_hz;
    bool cap_setup;  /* the profile has still to be written and the receiver started */
    bool cap_active; /* symbols are being taken */
    bool cap_finish; /* the user asked it to end */
    bool cap_rx;     /* the transport's receiver is running */
    uint16_t cap_noted;
    uint32_t cap_note_ms;
    subghz_sym_t take[SUBGHZ_TAKE_MAX];
    /* ---- replay (STARK-0605) ---- */
    subghz_txpolicy_t pol; /* the duty ledger: it outlives a lost and returned module */
    bool tx_pending;       /* one confirmed request has not been carried out yet */
    subghz_raw_t tx_raw;
    uint32_t tx_hash;
    char tx_name[16];
} subghz_svc_t;

void subghz_svc_init(subghz_svc_t *s, const subghz_svc_io_t *io);

/*
 * One round, bounded: at most one claim, one identification and one configuration,
 * or one meter sample or one scan channel. Returns the ms to wait before the next
 * round (1..SUBGHZ_IDLE_MS).
 */
uint32_t subghz_svc_step(subghz_svc_t *s);

bool subghz_svc_ready(const subghz_svc_t *s);

/*
 * The measurement requests (stark_subghz.h says what they mean). They change state
 * only; the next step does the radio's work. STARK_ERR_STATE unless ready,
 * STARK_ERR_INVALID_ARG for a frequency outside the band.
 */
stark_err_t subghz_svc_meter(subghz_svc_t *s, uint32_t hz);
stark_err_t subghz_svc_scan(subghz_svc_t *s);
/* buf must hold cap symbols and stay valid until the capture ends or is stopped. */
stark_err_t subghz_svc_capture(subghz_svc_t *s, subghz_mod_t mod, uint32_t hz, subghz_sym_t *buf,
                               size_t cap);
void subghz_svc_capture_finish(subghz_svc_t *s);
/*
 * The replay requests (stark_subghz.h says what they mean). subghz_svc_tx_check() changes
 * nothing but the log; subghz_svc_tx() on SUBGHZ_TX_OK leaves one request pending, and the next
 * step carries it out (checks it again, transmits once, idles the radio). A refusal is logged
 * through io.log_tx. subghz_svc_stop() drops a pending request.
 */
subghz_tx_verdict_t subghz_svc_tx_check(subghz_svc_t *s, const subghz_raw_t *raw, const char *name,
                                        subghz_tx_info_t *info);
subghz_tx_verdict_t subghz_svc_tx(subghz_svc_t *s, const subghz_raw_t *raw, const char *name,
                                  uint32_t hash, subghz_tx_info_t *info);
void subghz_svc_stop(subghz_svc_t *s);
void subghz_svc_status(const subghz_svc_t *s, stark_subghz_status_t *out);
