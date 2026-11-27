/*
 * stark_wifi_transport.h — the ESP32-S3's own Wi-Fi in promiscuous receive mode
 * (TASKS.md STARK-1005, ADR-0027, ADR-0028): the only code in the tree that
 * calls esp_wifi. It brings the radio up on demand, tunes a 2.4 GHz channel,
 * and hands received 802.11 frames (with their RSSI and channel) to the caller.
 * The radio is opened lazily by the service on app entry, never at boot
 * (ADR-0028). The emulator test image substitutes this component with one built
 * on a test-only fake.
 *
 * Receive-first by construction: there is NO transmit, inject, or raw-send call
 * here, and none is reachable through it (ADR-0027 §3). The offensive half of
 * comparable tools — deauth, beacon spam, any frame injection — is unimplemented
 * and unexposed, not merely disabled (docs/SECURITY_SCOPE.md §3).
 *
 * One caller at a time: the stark_wifi service task.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

/* Bytes of each frame the transport keeps: the MAC header plus the tagged
 * parameters we dissect fit well inside this; the rest of a frame is dropped. */
#define STARK_WIFI_FRAME_MAX 128u

/*
 * Bring the Wi-Fi radio up in promiscuous receive mode (station interface, no
 * association, no transmit). STARK_OK once it is receiving; an error, with the
 * radio left down, if the SoC's Wi-Fi does not initialise. Idempotent: a second
 * open while open is STARK_OK.
 */
stark_err_t stark_wifi_transport_open(void);

/* Stop promiscuous receive and bring the radio down. Safe whether or not open. */
void stark_wifi_transport_close(void);

/* Tune to a 2.4 GHz channel (1..14). STARK_ERR_INVALID_ARG outside that range,
 * STARK_ERR_STATE when closed. */
stark_err_t stark_wifi_transport_set_channel(uint8_t channel);

/*
 * Take one received frame. Copies up to cap bytes of it into buf and writes its
 * RSSI (dBm) and channel (1..14) through the out pointers. Returns the number of
 * bytes copied (1..cap), or 0 when none arrived within timeout_ms or the
 * transport is closed. Never blocks longer than timeout_ms. Frames longer than
 * cap are copied truncated — the dissector reads only the header and early
 * tagged parameters, which fit. buf/rssi/channel must be non-NULL when cap > 0.
 */
size_t stark_wifi_transport_rx(uint8_t *buf, size_t cap, int8_t *rssi, uint8_t *channel,
                               uint32_t timeout_ms);
