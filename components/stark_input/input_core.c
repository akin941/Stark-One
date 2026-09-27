/*
 * input_core.c — pure key state machine (TASKS.md STARK-0011).
 *
 * Includes only its own headers: no ESP-IDF, no FreeRTOS (AC #3).
 */
#include "input_core.h"

typedef struct {
    input_action_t *out;
    size_t max;
    size_t n;
} sink_t;

static void emit(sink_t *s, size_t k, stark_key_action_t action, uint8_t repeat)
{
    if (s->n < s->max) {
        s->out[s->n++] = (input_action_t){
            .key = (stark_key_t)k,
            .action = action,
            .repeat = repeat,
        };
    }
}

static bool key_repeats(size_t k)
{
    return k == STARK_KEY_UP || k == STARK_KEY_DOWN || k == STARK_KEY_LEFT || k == STARK_KEY_RIGHT;
}

static void emit_long(sink_t *s, size_t k, input_key_state_t *st)
{
    st->long_sent = true;
    emit(s, k, STARK_KEY_LONG, 0);
}

static void emit_repeat(sink_t *s, size_t k, input_key_state_t *st, uint32_t held,
                        uint16_t interval)
{
    if (st->repeats < UINT8_MAX) {
        st->repeats++;
    }
    emit(s, k, STARK_KEY_REPEAT, st->repeats);
    st->next_repeat += interval;
    if (st->next_repeat <= held) {
        /* Updates arrived late: skip the missed repeats rather than burst. */
        st->next_repeat = held + interval;
    }
}

/* `quiet`: a chord is latched and k is one of its keys — no SHORT, no LONG. */
static void update_key(sink_t *s, size_t k, input_key_state_t *st, bool raw, uint32_t now_ms,
                       const input_core_timing_t *t, bool quiet)
{
    if (raw != st->raw) {
        st->raw = raw;
        st->raw_since = now_ms;
    }
    bool stable = (uint32_t)(now_ms - st->raw_since) >= t->debounce_ms;

    if (!st->pressed) {
        if (raw && stable) {
            st->pressed = true;
            st->pressed_at = now_ms;
            st->next_repeat = t->repeat_delay_ms;
            st->repeats = 0;
            st->long_sent = false;
            emit(s, k, STARK_KEY_PRESS, 0);
        }
        return;
    }

    uint32_t held = now_ms - st->pressed_at;

    if (!raw && stable) {
        if (!quiet && !st->long_sent && held >= t->long_ms) {
            emit_long(s, k, st);
        }
        emit(s, k, STARK_KEY_RELEASE, 0);
        if (!quiet && !st->long_sent) {
            emit(s, k, STARK_KEY_SHORT, 0);
        }
        st->pressed = false;
        return;
    }

    /* Still held (a release not yet debounced counts as held). */
    bool long_due = !quiet && !st->long_sent && held >= t->long_ms;
    bool repeat_due = key_repeats(k) && held >= st->next_repeat;
    if (long_due && repeat_due && st->next_repeat < t->long_ms) {
        emit_repeat(s, k, st, held, t->repeat_interval_ms); /* the repeat fell due first */
        emit_long(s, k, st);
        return;
    }
    if (long_due) {
        emit_long(s, k, st);
    }
    if (repeat_due) {
        emit_repeat(s, k, st, held, t->repeat_interval_ms);
    }
}

stark_err_t input_core_init(input_core_t *c, const input_core_timing_t *t)
{
    static const input_core_timing_t k_default = {
        INPUT_DEBOUNCE_MS,
        INPUT_LONG_PRESS_MS,
        INPUT_REPEAT_DELAY_MS,
        INPUT_REPEAT_INTERVAL_MS,
    };
    if (t == NULL) {
        t = &k_default;
    }
    if (c == NULL || t->repeat_interval_ms < INPUT_REPEAT_MIN_MS ||
        t->repeat_delay_ms < t->debounce_ms || t->long_ms <= t->debounce_ms) {
        return STARK_ERR_INVALID_ARG;
    }
    *c = (input_core_t){.t = *t};
    return STARK_OK;
}

size_t input_core_update(input_core_t *c, uint8_t raw_bitmap, uint32_t now_ms, input_action_t *out,
                         size_t max_out)
{
    if (c == NULL) {
        return 0;
    }

    sink_t sink = {.out = out, .max = out == NULL ? 0 : max_out, .n = 0};
    for (size_t k = 0; k < STARK_KEY_COUNT; k++) {
        bool quiet = c->chord && (k == STARK_KEY_OK || k == STARK_KEY_BACK);
        update_key(&sink, k, &c->key[k], ((raw_bitmap >> k) & 1u) != 0, now_ms, &c->t, quiet);
    }

    bool ok = c->key[STARK_KEY_OK].pressed;
    bool back = c->key[STARK_KEY_BACK].pressed;
    if (!c->chord && ok && back) {
        c->chord = true;
        emit(&sink, STARK_KEY_OK, STARK_KEY_CHORD, 0);
    } else if (c->chord && !ok && !back) {
        c->chord = false; /* both released: a new chord may form */
    }
    return sink.n;
}
