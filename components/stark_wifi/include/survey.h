/*
 * survey.h — Wi-Fi channel/RSSI survey and capture record (TASKS.md
 * STARK-1003, ADR-0027): aggregate dissected 802.11 frames into a per-channel
 * survey, and keep a bounded ring of capture summaries with a documented,
 * round-trippable log-line format. Pure; the transport feeds it later.
 *
 * Fixed storage, no allocation after init. A capture summary holds only
 * channel/type/RSSI/BSSID/sequence — never a frame body or any key/EAPOL
 * material (SECURITY_SCOPE §2). The log line goes to the user's own card.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "dot11_frame.h"
#include "stark_err.h"

#define WIFI_SURVEY_CHANNELS   14u /* 2.4 GHz channels 1..14 */
#define WIFI_SURVEY_MAX_BSSIDS 32u /* distinct BSSIDs tracked (bounded) */
#define WIFI_CAPTURE_RING      64u /* capture summaries kept, oldest overwritten */
#define WIFI_CAPTURE_LINE      72u /* bytes a formatted log line needs incl. NUL */

/* Per-channel aggregate. rssi_* are valid only when frames > 0. */
typedef struct {
    uint32_t frames;
    int8_t rssi_min;
    int8_t rssi_max;
    int8_t rssi_last;
} wifi_chan_stat_t;

typedef struct {
    wifi_chan_stat_t chan[WIFI_SURVEY_CHANNELS]; /* index 0 == channel 1 */
    uint8_t cur_channel;                         /* radio's channel 1..14, 0 = unset */
    uint8_t bssids[WIFI_SURVEY_MAX_BSSIDS][6];   /* distinct BSSIDs, bounded */
    uint16_t bssid_count;                        /* distinct BSSIDs stored (<= max) */
} wifi_survey_t;

/* A capture summary — the only thing the ring stores. No frame body. */
typedef struct {
    uint8_t channel;
    uint8_t type;    /* dot11 type (0..3) */
    uint8_t subtype; /* dot11 subtype (0..15) */
    int8_t rssi;
    uint8_t bssid[6]; /* addr3 if present, else zeros */
    uint16_t seq;
} wifi_capture_rec_t;

typedef struct {
    wifi_capture_rec_t rec[WIFI_CAPTURE_RING];
    uint32_t head;  /* next write slot */
    uint32_t count; /* total pushed (saturating view is head/count) */
} wifi_capture_t;

/* Zero a survey. cur_channel becomes 0 (unset). */
void survey_init(wifi_survey_t *s);

/* The radio hopped to `channel` (1..14); frames without a DS channel bucket here. */
void survey_set_channel(wifi_survey_t *s, uint8_t channel);

/*
 * Fold one dissected frame at `rssi` into the survey. The channel is the
 * frame's DS channel when it carries one, else the current channel. Updates the
 * channel's frame count and RSSI min/max/last, and records a distinct BSSID
 * (the frame's addr3) while the bounded table has room. STARK_ERR_INVALID_ARG,
 * writing nothing, when the resolved channel is out of 1..14 (e.g. a frame with
 * no DS channel while cur_channel is unset). s and f must be non-NULL.
 */
stark_err_t survey_add(wifi_survey_t *s, const dot11_frame_t *f, int8_t rssi);

/* Zero a capture ring. */
void capture_init(wifi_capture_t *c);

/* Push one summary, overwriting the oldest when the ring is full. */
void capture_push(wifi_capture_t *c, const wifi_capture_rec_t *r);

/* How many summaries the ring currently holds (<= WIFI_CAPTURE_RING). */
uint32_t capture_len(const wifi_capture_t *c);

/*
 * Format a summary as the documented log line
 *   "ch=<n> t=<type>/<subtype> rssi=<dbm> bssid=<aa:bb:cc:dd:ee:ff> seq=<n>"
 * into out[0..n). Returns the length written (excluding NUL), or
 * STARK_ERR_INVALID_ARG for a NULL argument or a buffer smaller than
 * WIFI_CAPTURE_LINE. The line never contains a frame body.
 */
stark_err_t capture_format(const wifi_capture_rec_t *r, char *out, size_t n);
