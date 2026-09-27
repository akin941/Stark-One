/*
 * input_core.h — L2 core: per-key debounce, long-press and repeat state
 * machine (ARCHITECTURE.md §6.6, TASKS.md STARK-0011).
 *
 * Pure and deterministic: fed (raw_bitmap, now_ms) samples, it emits
 * actions into a caller-supplied array. No clock, no GPIO, no allocation —
 * the caller owns time. Each key is independent; there are no chords.
 *
 * Timing model (all relative to the debounced edge, i.e. to when PRESS is
 * emitted, so a release is measured the same way as a press):
 *  - a raw level must be stable for INPUT_DEBOUNCE_MS before it is taken;
 *  - LONG once, when a press has been held INPUT_LONG_PRESS_MS;
 *  - REPEAT at INPUT_REPEAT_DELAY_MS, then every INPUT_REPEAT_INTERVAL_MS,
 *    for UP/DOWN/LEFT/RIGHT only. At most one REPEAT per update: after a
 *    gap between updates, missed repeats are skipped, never burst;
 *  - on release: RELEASE, then SHORT unless LONG was emitted. A release
 *    that arrives after INPUT_LONG_PRESS_MS without LONG having been seen
 *    (a gap between updates) emits LONG then RELEASE, never SHORT.
 * Elapsed times use unsigned subtraction, so now_ms may wrap.
 *
 * Chord (STARK-0106): when OK and BACK are both debounced-pressed, one
 * CHORD action (key = OK) is emitted, and from then on SHORT and LONG are
 * suppressed for both keys (PRESS and RELEASE still flow) until both have
 * been released; only then can a new chord form. OK+BACK is reserved for a
 * future soft reset, so no app can ever act on it.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"
#include "stark_input.h"

/* Default timings (the V0 values); input_core_init() takes others. */
#define INPUT_DEBOUNCE_MS        20
#define INPUT_LONG_PRESS_MS      500
#define INPUT_REPEAT_DELAY_MS    400
#define INPUT_REPEAT_INTERVAL_MS 120
#define INPUT_REPEAT_MIN_MS      40 /* the shortest repeat interval accepted */

/* At most two actions per key per update (RELEASE+SHORT, LONG+RELEASE or
 * LONG+REPEAT) plus one CHORD: an out array this large never loses one. */
#define INPUT_CORE_MAX_ACTIONS   (2 * STARK_KEY_COUNT + 1)

typedef struct {
    uint16_t debounce_ms;
    uint16_t long_ms;
    uint16_t repeat_delay_ms;
    uint16_t repeat_interval_ms;
} input_core_timing_t;

typedef struct {
    stark_key_t key;
    stark_key_action_t action;
    uint8_t repeat; /* REPEAT: 1, 2, 3 … within this press (saturates at 255); else 0 */
} input_action_t;

/* Private to input_core.c; caller-allocated, so it must be complete. */
typedef struct {
    bool raw;             /* last sampled level */
    bool pressed;         /* debounced level */
    uint32_t raw_since;   /* now_ms when raw last changed */
    uint32_t pressed_at;  /* now_ms when PRESS was emitted */
    uint32_t next_repeat; /* ms after pressed_at when the next REPEAT is due */
    uint8_t repeats;      /* REPEATs emitted in this press */
    bool long_sent;       /* LONG emitted in this press */
} input_key_state_t;

typedef struct {
    input_key_state_t key[STARK_KEY_COUNT];
    input_core_timing_t t;
    bool chord; /* OK+BACK latched until both are released */
} input_core_t;

/*
 * Every key released, no history, with timing `t` (NULL: the defaults
 * above). STARK_ERR_INVALID_ARG — and c untouched — for a NULL c or a
 * timing with repeat_interval_ms < INPUT_REPEAT_MIN_MS, repeat_delay_ms <
 * debounce_ms, or long_ms <= debounce_ms.
 */
stark_err_t input_core_init(input_core_t *c, const input_core_timing_t *t);

/*
 * Advances every key to now_ms given the raw levels in raw_bitmap (bit k set
 * = key k physically pressed; bits above STARK_KEY_COUNT are ignored) and
 * writes the resulting actions to out, in key order, returning how many.
 * With max_out >= INPUT_CORE_MAX_ACTIONS nothing is lost; with less, the
 * excess actions are discarded but the state still advances. A NULL out is
 * treated as max_out == 0. Returns 0 and does nothing for a NULL c.
 */
size_t input_core_update(input_core_t *c, uint8_t raw_bitmap, uint32_t now_ms, input_action_t *out,
                         size_t max_out);
