/*
 * stark_buzzer.h — L3, the passive piezo on LEDC channel 0 (TASKS.md
 * STARK-0020, ARCHITECTURE.md §2, HARDWARE.md §2).
 *
 * Non-blocking: a tone or a sequence starts and the call returns; an
 * esp_timer steps through the notes. A new request replaces whatever is
 * playing. Task context only. Before stark_buzzer_init() every call is a
 * harmless no-op returning STARK_ERR_STATE (so UI feedback can call it
 * unconditionally).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

/* One note: hz == 0 is a rest. */
typedef struct {
    uint16_t hz;
    uint16_t ms;
} stark_buzzer_note_t;

#define STARK_BUZZER_MAX_NOTES 16

/* Attaches LEDC channel 0 to the board's buzzer pin, silent. A board
 * without a buzzer (pin -1) initialises fine and stays silent. */
stark_err_t stark_buzzer_init(void);

/* One tone of hz (200..10000) for ms (1..10000). STARK_ERR_INVALID_ARG out of range. */
stark_err_t stark_buzzer_tone(uint16_t hz, uint16_t ms);

/* Plays up to STARK_BUZZER_MAX_NOTES notes (copied: the caller's array may
 * go away). STARK_ERR_INVALID_ARG for NULL, 0 or too many notes, or a note
 * out of range. */
stark_err_t stark_buzzer_play(const stark_buzzer_note_t *notes, size_t count);

/* Silences the buzzer and drops the rest of any sequence. */
void stark_buzzer_stop(void);

/* UI feedback (TASKS.md STARK-0020): a 20 ms click on a selection change,
 * a 60 ms low buzz on a rejected action (BACK at the root, OK with nothing
 * selectable). */
void stark_buzzer_click(void);
void stark_buzzer_reject(void);
