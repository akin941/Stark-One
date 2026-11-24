/*
 * selftestapp.h — private to the Self-Test app (TASKS.md STARK-0707, ADR-0023 §8): the home rows
 * and the operator walk, as pure logic. selftest_views.c holds no service call and no UI call
 * (host-tested to every line); app_selftest.c drives the services, the worker and the screens.
 *
 * The walk is what the device cannot do for itself: after the automated checks it asks the
 * operator for each key (Buttons: the input service's own key events are the observation), then
 * shows the colour bars (Display), plays three tones (Buzzer) and names the status LED, and
 * records what the operator says: OK is "yes", BACK is "no" and ends the walk (what is left stays
 * `not run`). An operator's yes is `attested`, never `pass`.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_diag.h"

#define STAPP_KEYS               6u     /* UP DOWN LEFT RIGHT OK BACK */
#define STAPP_BUTTONS_TIMEOUT_MS 30000u /* the Buttons step ends after this long */
#define STAPP_ROWS_MAX           (STARK_SELFTEST_MAX + 3u)

/* The places of the checks the app observes itself in the table the root registers (the order of
 * main/selftest_checks.h, which test_selftest_checks.c compares against these). The checks before
 * STAPP_BUTTONS run on the app worker. */
enum { STAPP_BUTTONS = 6, STAPP_DISPLAY = 7, STAPP_BUZZER = 8, STAPP_LED = 9 };

/* ---- home rows ---------------------------------------------------------------------------- */

typedef enum {
    STROW_RUN,     /* action: run everything */
    STROW_SUMMARY, /* a dimmed line: the counts */
    STROW_CHECK,   /* check `idx`: its result is the value; OK shows its evidence */
    STROW_EXPORT   /* action: write the report to the card */
} strow_kind_t;

typedef struct {
    strow_kind_t kind;
    uint8_t idx;
} strow_t;

/* Run all, the summary, one row per check, Export report. Returns how many (at most
 * STAPP_ROWS_MAX). */
size_t stview_rows(size_t checks, strow_t *out);

/* A row's label: a constant, the check's name, or `summary` for the summary row. Never NULL. */
const char *stview_label(const strow_t *row, const stark_selftest_row_t *results, size_t n,
                         const char *summary);

/* A check row's value: "pass", "FAIL", "skip", "attested" or "-"; NULL for any other row. */
const char *stview_value(const strow_t *row, const stark_selftest_row_t *results, size_t n);

/* The summary line for the home list: "7 pass  3 attested  0 fail  0 skip", and, while some checks
 * are not run, "6 pass  0 att  1 fail  1 skip  2 left" (a screen row holds 38 characters). */
void stview_summary(const stark_selftest_row_t *results, size_t n, char *buf, size_t cap);

/* A result in words: "not run", "pass", "fail", "skip" or "attested". */
const char *stview_result_word(stark_selftest_result_t r);

/* ---- the operator walk -------------------------------------------------------------------- */

typedef enum {
    STWALK_NONE,
    STWALK_BUTTONS,
    STWALK_DISPLAY,
    STWALK_BUZZER,
    STWALK_LED
} stwalk_step_t;

typedef struct {
    stwalk_step_t step;
    uint8_t seen;    /* keys pressed in the Buttons step, one bit each */
    uint8_t pressed; /* keys whose press arrived in this step: only those may answer it */
    uint32_t started_ms;
} stwalk_t;

typedef struct {
    bool consumed; /* the event belonged to the walk (every key event does) */
    bool have;     /* a result to record: index, result, evidence */
    size_t index;  /* the check's place in the table */
    stark_selftest_result_t result;
    char evidence[STARK_SELFTEST_EVIDENCE + 1u];
    bool entered;  /* the step changed: redraw, and play the tones for STWALK_BUZZER */
    bool replay;   /* UP in the Buzzer step: play the tones again */
    bool finished; /* the walk is over, completed or ended by BACK */
} stwalk_out_t;

/* Begins at the Buttons step. */
void stwalk_start(stwalk_t *w, uint32_t now_ms);

/* One key event (key 0..5, action a stark_key_action_t value). */
void stwalk_key(stwalk_t *w, uint8_t key, uint8_t action, stwalk_out_t *out);

/* The clock moving on (once a second is enough): the Buttons step ends after 30 s. */
void stwalk_tick(stwalk_t *w, uint32_t now_ms, stwalk_out_t *out);

/* Texts of a step: its title, its question or instruction, and the answer line. */
const char *stview_step_title(stwalk_step_t step);
const char *stview_step_text(stwalk_step_t step);
const char *stview_step_keys(stwalk_step_t step);

/* The name of key 0..5, "?" past that. */
const char *stview_key_name(uint8_t key);

/* The Buttons prompt: "Press UP" with the seconds left, or "All keys seen". */
void stview_buttons_prompt(const stwalk_t *w, uint32_t now_ms, char *buf, size_t n);
