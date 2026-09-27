/*
 * ui_port.h — control surface of the host UI test port (Tier 4B,
 * docs/VALIDATION.md): production stark_ui / stark_app / stark_gfx code runs
 * on the host against test backends of stark_display (a 320x240 framebuffer
 * filled band by band with the production display_bands() walk),
 * stark_event (the production core, no locks), stark_buzzer (recorder) and
 * esp_log (capture). Tests drive it with key events and assert on pixels,
 * render areas, log lines and buzzer calls. Test-only; the firmware never
 * sees this directory.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_gfx.h"
#include "stark_input.h"

#define UI_PORT_W 320
#define UI_PORT_H 240

/* Clears render areas, captured log lines and buzzer counts; the framebuffer
 * keeps the last frame (it starts poisoned before the first one). */
void ui_port_reset_records(void);

/* ---- display -------------------------------------------------------- */
#define UI_PORT_POISON 0xF81Fu /* magenta: pixels no screen drew */
const uint16_t *ui_port_fb(void);
uint16_t ui_port_pixel(int x, int y);
uint64_t ui_port_fb_hash(void); /* FNV-1a over the framebuffer */
size_t ui_port_render_count(void);
gfx_rect_t ui_port_render_area(size_t i); /* as requested by stark_ui */
void ui_port_set_band_h(int16_t band_h);  /* default: CONFIG_STARK_DISPLAY_BAND_H */
void ui_port_display_fail_next(void);     /* next render returns STARK_ERR_TIMEOUT */
bool ui_port_dump_ppm(const char *path);

/* ---- buzzer --------------------------------------------------------- */
int ui_port_buzzer_clicks(void);
int ui_port_buzzer_rejects(void);

/* ---- log ------------------------------------------------------------ */
size_t ui_port_log_count(void);
const char *ui_port_log_line(size_t i);
bool ui_port_log_contains(const char *needle);

/* ---- input ---------------------------------------------------------- */
/* Publishes one STARK_EVT_KEY, as the input service would. */
void ui_port_key(stark_key_t key, stark_key_action_t action);
/* A full short press: PRESS then SHORT (what the core emits on release). */
void ui_port_short(stark_key_t key);
