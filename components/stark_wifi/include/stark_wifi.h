/*
 * stark_wifi.h — L3 Wi-Fi analysis service (TASKS.md STARK-1005, ADR-0027,
 * ADR-0028): brings the ESP32-S3's own Wi-Fi up in promiscuous receive on
 * demand and pumps frames through the pure cores (dot11_frame, survey, wifi_svc).
 *
 * Receive-first: the service and its transport expose no transmit path at all
 * (ADR-0027 §3). The radio is opened lazily — `stark_wifi_start()` from the
 * analysis app, never at boot (ADR-0028) — so the emulator never runs the
 * unmodelled RF path and production scenes boot unchanged. State changes are
 * published as STARK_EVT_SYSTEM / STARK_SYS_WIFI with system.arg =
 * STARK_WIFI_EVT_MODULE.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stark_err.h"
#include "survey.h"

#define STARK_SYS_WIFI        10u

#define STARK_WIFI_EVT_MODULE 1 /* stark_wifi_ready() changed */

/* Start the dormant service task (core 0, below the UI). The radio stays down
 * until stark_wifi_start(); never fails the boot. Call after stark_event_init(). */
stark_err_t stark_wifi_init(void);

/*
 * Bring the radio up in promiscuous receive and begin folding frames into the
 * survey. STARK_OK when it is receiving; an error, radio left down, if the SoC's
 * Wi-Fi does not initialise. Idempotent while running.
 */
stark_err_t stark_wifi_start(void);

/* Bring the radio down and stop receiving. Safe whether or not started. */
void stark_wifi_stop(void);

/* True while the radio is up and receiving. Any task. */
bool stark_wifi_ready(void);

/* Copy a consistent snapshot of the survey for a reader (the app). out must be
 * non-NULL; zeros it when the service has no data yet. */
void stark_wifi_survey_copy(wifi_survey_t *out);
