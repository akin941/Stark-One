/*
 * subghz_rssi.h — RSSI conversion, text, and the band-scan state machine
 * (TASKS.md STARK-0602, ADR-0022). Pure: no I/O, no ESP-IDF. The scan drives the
 * radio only through an injected probe, so the host tests can inject any
 * sequence of readings and failures.
 *
 * Levels are carried as twice the dBm (an int16), because the chip reports half
 * dB steps: dBm = (raw >= 128 ? (raw - 256) / 2 : raw / 2) - 74.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

#define SUBGHZ_RSSI_OFFSET_X2  148 /* 74 dBm, doubled */

/* The one band the radio may be tuned to (ADR-0022 §5): a constant, never an option. */
#define SUBGHZ_BAND_MIN_HZ     433050000u
#define SUBGHZ_BAND_MAX_HZ     434790000u

/* The scan (TASKS.md STARK-0602): 25 kHz steps from 433.075 to 434.775 MHz, 69 channels. */
#define SUBGHZ_SCAN_FIRST_HZ   433075000u
#define SUBGHZ_SCAN_STEP_HZ    25000u
#define SUBGHZ_SCAN_CHANNELS   69u
#define SUBGHZ_SCAN_SETTLE_MS  1u /* after each retune, before the first read */
#define SUBGHZ_SCAN_READS      4u /* the maximum of these is the channel's level */
#define SUBGHZ_METER_STEP_HZ   25000u
#define SUBGHZ_METER_PERIOD_MS 50u

/* Twice the dBm for a raw RSSI byte: -138 .. -10.5 dBm. */
int16_t subghz_rssi_dbm_x2(uint8_t raw);

/* "-40" or "-40.5" (a whole number of dB drops the fraction). Always terminated. */
void subghz_dbm_text(char *out, size_t size, int16_t dbm_x2);
/* "433.925": MHz with kHz resolution. */
void subghz_mhz_text(char *out, size_t size, uint32_t hz);

/* ---- band scan ----------------------------------------------------------- */

typedef struct {
    stark_err_t (*tune)(void *ctx, uint32_t hz);  /* retune and receive */
    stark_err_t (*read)(void *ctx, uint8_t *raw); /* one raw RSSI byte */
    void (*delay_ms)(void *ctx, uint32_t ms);
    void *ctx;
} subghz_probe_t;

typedef enum {
    SUBGHZ_SCAN_IDLE, /* not started */
    SUBGHZ_SCAN_RUNNING,
    SUBGHZ_SCAN_DONE,
    SUBGHZ_SCAN_FAILED
} subghz_scan_state_t;

typedef struct {
    subghz_probe_t probe;
    subghz_scan_state_t state;
    uint8_t next;        /* the next channel index */
    int16_t peak_dbm_x2; /* the strongest channel so far */
    uint32_t peak_hz;
    stark_err_t err; /* why the sweep failed */
} subghz_scan_t;

/* Begins a sweep at the first channel. */
void subghz_scan_start(subghz_scan_t *s, const subghz_probe_t *probe);

/*
 * One channel: tune, settle, four reads. Returns the state afterwards (RUNNING until
 * the 69th channel is done, FAILED on the first tune or read error). In any state
 * but RUNNING it does nothing. The strongest channel wins; on a tie the lowest.
 */
subghz_scan_state_t subghz_scan_step(subghz_scan_t *s);

/* 0..100. */
uint8_t subghz_scan_percent(const subghz_scan_t *s);
