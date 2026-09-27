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

static void emit_repeat(sink_t *s, size_t k, input_key_state_t *st, uint32_t held)
{
    if (st->repeats < UINT8_MAX) {
        st->repeats++;
    }
    emit(s, k, STARK_KEY_REPEAT, st->repeats);
    st->next_repeat += INPUT_REPEAT_INTERVAL_MS;
    if (st->next_repeat <= held) {
        /* Updates arrived late: skip the missed repeats rather than burst. */
        st->next_repeat = held + INPUT_REPEAT_INTERVAL_MS;
    }
}

static void update_key(sink_t *s, size_t k, input_key_state_t *st, bool raw, uint32_t now_ms)
{
    if (raw != st->raw) {
        st->raw = raw;
        st->raw_since = now_ms;
    }
    bool stable = (uint32_t)(now_ms - st->raw_since) >= INPUT_DEBOUNCE_MS;

    if (!st->pressed) {
        if (raw && stable) {
            st->pressed = true;
            st->pressed_at = now_ms;
            st->next_repeat = INPUT_REPEAT_DELAY_MS;
            st->repeats = 0;
            st->long_sent = false;
            emit(s, k, STARK_KEY_PRESS, 0);
        }
        return;
    }

    uint32_t held = now_ms - st->pressed_at;

    if (!raw && stable) {
        if (!st->long_sent && held >= INPUT_LONG_PRESS_MS) {
            emit_long(s, k, st);
        }
        emit(s, k, STARK_KEY_RELEASE, 0);
        if (!st->long_sent) {
            emit(s, k, STARK_KEY_SHORT, 0);
        }
        st->pressed = false;
        return;
    }

    /* Still held (a release not yet debounced counts as held). */
    bool long_due = !st->long_sent && held >= INPUT_LONG_PRESS_MS;
    bool repeat_due = key_repeats(k) && held >= st->next_repeat;
    if (long_due && repeat_due && st->next_repeat < INPUT_LONG_PRESS_MS) {
        emit_repeat(s, k, st, held); /* the repeat fell due first */
        emit_long(s, k, st);
        return;
    }
    if (long_due) {
        emit_long(s, k, st);
    }
    if (repeat_due) {
        emit_repeat(s, k, st, held);
    }
}

void input_core_init(input_core_t *c)
{
    if (c != NULL) {
        *c = (input_core_t){0};
    }
}

size_t input_core_update(input_core_t *c, uint8_t raw_bitmap, uint32_t now_ms, input_action_t *out,
                         size_t max_out)
{
    if (c == NULL) {
        return 0;
    }

    sink_t sink = {.out = out, .max = out == NULL ? 0 : max_out, .n = 0};
    for (size_t k = 0; k < STARK_KEY_COUNT; k++) {
        update_key(&sink, k, &c->key[k], ((raw_bitmap >> k) & 1u) != 0, now_ms);
    }
    return sink.n;
}
