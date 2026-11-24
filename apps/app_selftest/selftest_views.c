/*
 * selftest_views.c — the Self-Test app's rows and operator walk (selftestapp.h). Pure; host-tested
 * to every line.
 */
#include <stdio.h>
#include <string.h>
#include "selftestapp.h"
#include "stark_input.h"

#define ALL_KEYS 0x3Fu

static const char *const k_key_names[STAPP_KEYS] = {"UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK"};

/* ---- home rows ---------------------------------------------------------------------------- */

size_t stview_rows(size_t checks, strow_t *out)
{
    size_t n = 0;
    out[n++] = (strow_t){STROW_RUN, 0};
    out[n++] = (strow_t){STROW_SUMMARY, 0};
    for (size_t i = 0; i < checks && n < STAPP_ROWS_MAX - 1u; i++) {
        out[n++] = (strow_t){STROW_CHECK, (uint8_t)i};
    }
    out[n++] = (strow_t){STROW_EXPORT, 0};
    return n;
}

const char *stview_label(const strow_t *row, const stark_selftest_row_t *results, size_t n,
                         const char *summary)
{
    switch (row->kind) {
        case STROW_RUN:
            return "Run all";
        case STROW_SUMMARY:
            return summary != NULL ? summary : "";
        case STROW_CHECK:
            return row->idx < n ? results[row->idx].name : "?";
        case STROW_EXPORT:
        default:
            return "Export report";
    }
}

const char *stview_value(const strow_t *row, const stark_selftest_row_t *results, size_t n)
{
    if (row->kind != STROW_CHECK || row->idx >= n) {
        return NULL;
    }
    switch (results[row->idx].result) {
        case ST_PASS:
            return "pass";
        case ST_FAIL:
            return "FAIL";
        case ST_SKIP:
            return "skip";
        case ST_ATTESTED:
            return "attested";
        case ST_NOT_RUN:
        default:
            return "-";
    }
}

void stview_summary(const stark_selftest_row_t *results, size_t n, char *buf, size_t cap)
{
    unsigned count[5] = {0};
    for (size_t i = 0; i < n; i++) {
        if ((unsigned)results[i].result < 5u) {
            count[results[i].result]++;
        }
    }
    if (count[ST_NOT_RUN] == 0u) {
        (void)snprintf(buf, cap, "%u pass  %u attested  %u fail  %u skip", count[ST_PASS],
                       count[ST_ATTESTED], count[ST_FAIL], count[ST_SKIP]);
    } else {
        (void)snprintf(buf, cap, "%u pass  %u att  %u fail  %u skip  %u left", count[ST_PASS],
                       count[ST_ATTESTED], count[ST_FAIL], count[ST_SKIP], count[ST_NOT_RUN]);
    }
}

const char *stview_result_word(stark_selftest_result_t r)
{
    switch (r) {
        case ST_PASS:
            return "pass";
        case ST_FAIL:
            return "fail";
        case ST_SKIP:
            return "skip";
        case ST_ATTESTED:
            return "attested";
        case ST_NOT_RUN:
        default:
            return "not run";
    }
}

/* ---- the operator walk -------------------------------------------------------------------- */

static void record(stwalk_out_t *out, size_t index, stark_selftest_result_t r, const char *text)
{
    out->have = true;
    out->index = index;
    out->result = r;
    (void)snprintf(out->evidence, sizeof out->evidence, "%s", text);
}

/* Moves to the next step, or ends the walk after the LED. */
static void advance(stwalk_t *w, stwalk_out_t *out)
{
    w->pressed = 0;
    switch (w->step) {
        case STWALK_BUTTONS:
            w->step = STWALK_DISPLAY;
            out->entered = true;
            break;
        case STWALK_DISPLAY:
            w->step = STWALK_BUZZER;
            out->entered = true;
            break;
        case STWALK_BUZZER:
            w->step = STWALK_LED;
            out->entered = true;
            break;
        case STWALK_LED:
        case STWALK_NONE:
        default:
            w->step = STWALK_NONE;
            out->finished = true;
            break;
    }
}

void stwalk_start(stwalk_t *w, uint32_t now_ms)
{
    *w = (stwalk_t){.step = STWALK_BUTTONS, .started_ms = now_ms};
}

/* What the operator said about the step they are looking at. */
static void answer(stwalk_t *w, bool yes, stwalk_out_t *out)
{
    static const char *const k_yes[] = {"", "", "operator: bars and ramp look right",
                                        "operator: heard three tones",
                                        "operator: blinks once a second"};
    static const char *const k_no[] = {"", "", "operator: not right", "operator: heard nothing",
                                       "operator: no blink"};
    static const size_t k_index[] = {0, 0, STAPP_DISPLAY, STAPP_BUZZER, STAPP_LED};
    size_t s = (size_t)w->step;
    record(out, k_index[s], yes ? ST_ATTESTED : ST_FAIL, yes ? k_yes[s] : k_no[s]);
    if (yes) {
        advance(w, out);
    } else {
        w->step = STWALK_NONE; /* BACK ends the walk: what is left stays `not run` */
        out->finished = true;
    }
}

void stwalk_key(stwalk_t *w, uint8_t key, uint8_t action, stwalk_out_t *out)
{
    memset(out, 0, sizeof *out);
    if (w->step == STWALK_NONE) {
        return;
    }
    out->consumed = true;
    if (key >= STAPP_KEYS) {
        return;
    }
    uint8_t bit = (uint8_t)(1u << key);
    if (action == STARK_KEY_PRESS) {
        w->pressed |= bit;
        if (w->step == STWALK_BUTTONS) {
            w->seen |= bit;
            if (w->seen == ALL_KEYS) {
                record(out, STAPP_BUTTONS, ST_PASS, "all 6 keys seen");
                advance(w, out);
            }
        } else if (w->step == STWALK_BUZZER && key == STARK_KEY_UP) {
            out->replay = true;
        }
        return;
    }
    /* NOTE: an answer needs the press to have arrived in this step, so the release and short press
     * of the key that finished the previous one (OK or BACK) cannot answer the next. */
    if (action != STARK_KEY_SHORT || w->step == STWALK_BUTTONS || (w->pressed & bit) == 0u) {
        return;
    }
    if (key == STARK_KEY_OK) {
        answer(w, true, out);
    } else if (key == STARK_KEY_BACK) {
        answer(w, false, out);
    }
}

void stwalk_tick(stwalk_t *w, uint32_t now_ms, stwalk_out_t *out)
{
    memset(out, 0, sizeof *out);
    if (w->step != STWALK_BUTTONS || now_ms - w->started_ms < STAPP_BUTTONS_TIMEOUT_MS) {
        return;
    }
    out->consumed = true;
    char text[STARK_SELFTEST_EVIDENCE + 1u] = "missing";
    size_t len = strlen(text);
    const char *sep = " ";
    for (uint8_t k = 0; k < STAPP_KEYS; k++) {
        if ((w->seen & (1u << k)) == 0u) {
            len += (size_t)snprintf(text + len, sizeof text - len, "%s%s", sep, k_key_names[k]);
            sep = ",";
        }
    }
    record(out, STAPP_BUTTONS, ST_FAIL, text);
    advance(w, out);
}

const char *stview_step_title(stwalk_step_t step)
{
    switch (step) {
        case STWALK_BUTTONS:
            return "Buttons";
        case STWALK_DISPLAY:
            return "Display";
        case STWALK_BUZZER:
            return "Buzzer";
        case STWALK_LED:
            return "Status LED";
        case STWALK_NONE:
        default:
            return "Self-Test";
    }
}

const char *stview_step_text(stwalk_step_t step)
{
    switch (step) {
        case STWALK_BUTTONS:
            return "Press each key once";
        case STWALK_DISPLAY:
            return "Colour bars and grey ramp: right?";
        case STWALK_BUZZER:
            return "Three rising tones: heard?";
        case STWALK_LED:
            return "Does the LED blink once a second?";
        case STWALK_NONE:
        default:
            return "";
    }
}

const char *stview_step_keys(stwalk_step_t step)
{
    switch (step) {
        case STWALK_DISPLAY:
        case STWALK_LED:
            return "OK yes    BACK no";
        case STWALK_BUZZER:
            return "OK heard  BACK not  UP again";
        case STWALK_BUTTONS:
        case STWALK_NONE:
        default:
            return "";
    }
}

const char *stview_key_name(uint8_t key)
{
    return key < STAPP_KEYS ? k_key_names[key] : "?";
}

void stview_buttons_prompt(const stwalk_t *w, uint32_t now_ms, char *buf, size_t n)
{
    for (uint8_t k = 0; k < STAPP_KEYS; k++) {
        if ((w->seen & (1u << k)) == 0u) {
            uint32_t elapsed = now_ms - w->started_ms;
            uint32_t left = elapsed >= STAPP_BUTTONS_TIMEOUT_MS
                                ? 0u
                                : (STAPP_BUTTONS_TIMEOUT_MS - elapsed + 999u) / 1000u;
            (void)snprintf(buf, n, "Press %s  (%lu s left)", k_key_names[k], (unsigned long)left);
            return;
        }
    }
    (void)snprintf(buf, n, "All keys seen");
}
