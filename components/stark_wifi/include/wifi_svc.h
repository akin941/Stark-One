/*
 * wifi_svc.h — the pure Wi-Fi analysis step (TASKS.md STARK-1004, ADR-0027):
 * fold one received 802.11 frame into the survey and the capture ring. This is
 * the host-testable heart of the service; stark_wifi.c (target) only pumps the
 * transport into it on the service task. No ESP-IDF, no I/O.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"
#include "survey.h"

typedef struct {
    wifi_survey_t survey;
    wifi_capture_t capture;
} wifi_svc_t;

/* Zero the survey and capture ring. */
void wifi_svc_init(wifi_svc_t *s);

/*
 * Dissect one frame (buf[0..len), received at `rssi` on `channel` 1..14) and
 * fold it into the survey and capture ring. A frame that does not dissect, or a
 * channel out of 1..14, is dropped without touching the aggregates and returns
 * STARK_ERR_INVALID_ARG; STARK_OK when it was counted. Never stores a frame body
 * (SECURITY_SCOPE §2). s and buf must be non-NULL.
 */
stark_err_t wifi_svc_on_frame(wifi_svc_t *s, const uint8_t *buf, size_t len, int8_t rssi,
                              uint8_t channel);
