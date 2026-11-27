/*
 * survey.c — Wi-Fi channel/RSSI survey and capture ring (STARK-1003, ADR-0027).
 * Pure, fixed storage, no allocation after init. Summaries only — no frame
 * body, no credential material (SECURITY_SCOPE §2).
 */
#include "survey.h"

#include <stdio.h>
#include <string.h>

void survey_init(wifi_survey_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
    }
}

void survey_set_channel(wifi_survey_t *s, uint8_t channel)
{
    if (s != NULL && channel >= 1u && channel <= WIFI_SURVEY_CHANNELS) {
        s->cur_channel = channel;
    }
}

/* Record the frame's BSSID (addr3) in the bounded distinct table if it is new. */
static void survey_track_bssid(wifi_survey_t *s, const uint8_t *bssid)
{
    for (uint16_t i = 0; i < s->bssid_count; i++) {
        if (memcmp(s->bssids[i], bssid, 6) == 0) {
            return;
        }
    }
    if (s->bssid_count < WIFI_SURVEY_MAX_BSSIDS) {
        memcpy(s->bssids[s->bssid_count], bssid, 6);
        s->bssid_count++;
    }
}

stark_err_t survey_add(wifi_survey_t *s, const dot11_frame_t *f, int8_t rssi)
{
    if (s == NULL || f == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    uint8_t ch = (f->has_channel && f->channel >= 1u && f->channel <= WIFI_SURVEY_CHANNELS)
                     ? f->channel
                     : s->cur_channel;
    if (ch < 1u || ch > WIFI_SURVEY_CHANNELS) {
        return STARK_ERR_INVALID_ARG; /* no resolvable channel: write nothing */
    }

    wifi_chan_stat_t *st = &s->chan[ch - 1u];
    if (st->frames == 0u) {
        st->rssi_min = rssi;
        st->rssi_max = rssi;
    } else {
        if (rssi < st->rssi_min) {
            st->rssi_min = rssi;
        }
        if (rssi > st->rssi_max) {
            st->rssi_max = rssi;
        }
    }
    st->rssi_last = rssi;
    st->frames++;

    if (f->addr3 != NULL) {
        survey_track_bssid(s, f->addr3);
    }
    return STARK_OK;
}

void capture_init(wifi_capture_t *c)
{
    if (c != NULL) {
        memset(c, 0, sizeof(*c));
    }
}

void capture_push(wifi_capture_t *c, const wifi_capture_rec_t *r)
{
    if (c == NULL || r == NULL) {
        return;
    }
    c->rec[c->head] = *r;
    c->head = (c->head + 1u) % WIFI_CAPTURE_RING;
    c->count++;
}

uint32_t capture_len(const wifi_capture_t *c)
{
    if (c == NULL) {
        return 0u;
    }
    return (c->count < WIFI_CAPTURE_RING) ? c->count : WIFI_CAPTURE_RING;
}

stark_err_t capture_format(const wifi_capture_rec_t *r, char *out, size_t n)
{
    if (r == NULL || out == NULL || n < WIFI_CAPTURE_LINE) {
        return STARK_ERR_INVALID_ARG;
    }
    int len = snprintf(out, n, "ch=%u t=%u/%u rssi=%d bssid=%02x:%02x:%02x:%02x:%02x:%02x seq=%u",
                       r->channel, r->type, r->subtype, r->rssi, r->bssid[0], r->bssid[1],
                       r->bssid[2], r->bssid[3], r->bssid[4], r->bssid[5], r->seq);
    if (len < 0 || (size_t)len >= n) {
        return STARK_ERR_INVALID_ARG;
    }
    return STARK_OK;
}
