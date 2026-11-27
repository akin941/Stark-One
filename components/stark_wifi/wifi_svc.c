/*
 * wifi_svc.c — the pure Wi-Fi analysis step (STARK-1004, ADR-0027). Dissect a
 * received frame and fold it into the survey and the capture ring. Pure; built
 * from the STARK-1001/1003 cores. No frame body is retained (SECURITY_SCOPE §2).
 */
#include "wifi_svc.h"

#include <string.h>

#include "dot11_frame.h"

void wifi_svc_init(wifi_svc_t *s)
{
    if (s != NULL) {
        survey_init(&s->survey);
        capture_init(&s->capture);
    }
}

stark_err_t wifi_svc_on_frame(wifi_svc_t *s, const uint8_t *buf, size_t len, int8_t rssi,
                              uint8_t channel)
{
    if (s == NULL || buf == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (channel < 1u || channel > WIFI_SURVEY_CHANNELS) {
        return STARK_ERR_INVALID_ARG;
    }
    dot11_frame_t f;
    if (dot11_parse(buf, len, &f) != STARK_OK) {
        return STARK_ERR_INVALID_ARG; /* malformed frame: drop, change nothing */
    }

    /* The radio's channel is the ground truth for where this frame was heard;
     * survey_add still prefers a beacon's own DS channel when it carries one. */
    survey_set_channel(&s->survey, channel);
    stark_err_t err = survey_add(&s->survey, &f, rssi);
    if (err != STARK_OK) {
        return err;
    }

    wifi_capture_rec_t r;
    memset(&r, 0, sizeof r);
    r.channel = channel;
    r.type = (uint8_t)f.type;
    r.subtype = f.subtype;
    r.rssi = rssi;
    r.seq = f.has_seq ? f.seq : 0u;
    if (f.addr3 != NULL) {
        memcpy(r.bssid, f.addr3, 6);
    }
    capture_push(&s->capture, &r);
    return STARK_OK;
}
