/*
 * ir_symbols.c — RMT symbol words <-> timings (ir_symbols.h). Pure.
 */
#include "ir_symbols.h"
#include <stdbool.h>

static uint32_t duration(uint32_t word, int half)
{
    return half ? (word >> 16) & 0x7FFFu : word & 0x7FFFu;
}

static uint32_t level(uint32_t word, int half)
{
    return half ? word >> 31 : (word >> 15) & 1u;
}

/* Closes the current run into out; false when it does not fit. */
static bool flush(ir_timings_t *out, uint32_t run)
{
    if (run > UINT16_MAX || out->n >= IR_MAX_DURATIONS) {
        return false;
    }
    out->us[out->n++] = (uint16_t)run;
    return true;
}

bool ir_symbols_to_timings(const uint32_t *words, size_t n, ir_timings_t *out)
{
    out->n = 0;
    bool started = false;
    uint32_t current = 0;
    uint32_t run = 0;
    for (size_t i = 0; i < 2 * n; i++) {
        uint32_t d = duration(words[i / 2], (int)(i % 2));
        if (d == 0) {
            break; /* the RMT end marker */
        }
        uint32_t l = level(words[i / 2], (int)(i % 2));
        if (!started) {
            started = true; /* the first level seen is the mark */
            current = l;
        }
        if (l != current) {
            if (!flush(out, run)) {
                return false;
            }
            current = l;
            run = 0;
        }
        run += d;
    }
    return run == 0 || flush(out, run);
}

/* Appends one half (level, duration <= IR_SYMBOL_MAX_DURATION). */
static bool put_half(uint32_t *words, size_t max, size_t *n, int *half, uint32_t l, uint32_t d)
{
    if (*half == 0) {
        if (*n >= max) {
            return false;
        }
        words[*n] = (d & 0x7FFFu) | (l << 15);
        *half = 1;
    } else {
        words[*n] |= ((d & 0x7FFFu) << 16) | (l << 31);
        (*n)++;
        *half = 0;
    }
    return true;
}

bool ir_timings_to_symbols(const ir_timings_t *t, uint32_t *words, size_t max, size_t *n)
{
    *n = 0;
    int half = 0;
    for (uint16_t i = 0; i < t->n; i++) {
        uint32_t l = (i % 2 == 0) ? 1u : 0u;
        uint32_t d = t->us[i];
        while (d > IR_SYMBOL_MAX_DURATION) {
            if (!put_half(words, max, n, &half, l, IR_SYMBOL_MAX_DURATION)) {
                return false;
            }
            d -= IR_SYMBOL_MAX_DURATION;
        }
        if (!put_half(words, max, n, &half, l, d)) {
            return false;
        }
    }
    if (half == 1) {
        (*n)++; /* the last word's second half stays zero: the end marker */
    }
    return true;
}
