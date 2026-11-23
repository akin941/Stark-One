/*
 * ui_port_subghz.h — host UI test port: a stark_subghz backend the test drives
 * (TASKS.md STARK-0602/0603): module readiness, the meter's level, a finished sweep,
 * the symbols of a capture, and what the app asked of the radio.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stark_subghz.h"

void ui_port_subghz_reset(void);
/* A module came or went; publishes MODULE. A module that goes also ends any measurement. */
void ui_port_subghz_set_ready(bool ready);
/* The meter read a new level (twice the dBm); publishes RSSI. */
void ui_port_subghz_set_level(int16_t dbm_x2);
/* The sweep finished at a peak; publishes SCAN_DONE. */
void ui_port_subghz_finish_scan(uint32_t hz, int16_t dbm_x2);

/* The next starts fail with err (STARK_OK: they work again), as when the module left
 * and the UI has not been told yet. */
void ui_port_subghz_refuse(stark_err_t err);

/* A capture, as the real service keeps it (subghz_cap_feed over the app's buffer): symbols
 * arrive (publishes CAPTURE), and the capture ends (publishes CAPTURE_DONE) as the user's Stop,
 * the symbol limit or the 2 s limit would end it. */
void ui_port_subghz_capture_feed(const subghz_sym_t *s, size_t n);
void ui_port_subghz_capture_end(void);
unsigned ui_port_subghz_capture_starts(void);
unsigned ui_port_subghz_capture_finishes(void); /* the app asked it to end */
subghz_mod_t ui_port_subghz_capture_mod(void);  /* of the last start */
uint32_t ui_port_subghz_capture_hz(void);

/* True between a start and the next stop (the radio is receiving). */
bool ui_port_subghz_receiving(void);
uint32_t ui_port_subghz_meter_hz(void); /* the frequency of the last meter start */
unsigned ui_port_subghz_meter_starts(void);
unsigned ui_port_subghz_scan_starts(void);
unsigned ui_port_subghz_stops(void);
