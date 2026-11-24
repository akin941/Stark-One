/*
 * lab_autobaud.c — auto-baud detection (lab_autobaud.h). Pure; integer arithmetic only, every loop
 * bounded by the number of edges (at most LAB_AUTOBAUD_MAX_EDGES).
 */
#include "lab_autobaud.h"
#include <stdio.h>
#include "lab_uart.h"

#define STEP_BACKWARDS 0x80000000u /* a difference this big is a stamp that went backwards */
#define REFINE_ROUNDS  2u

static uint64_t abs_diff(uint64_t a, uint64_t b)
{
    return a > b ? a - b : b - a;
}

/* The listed baud whose bit time is nearest to a pulse of `ticks`: the one minimising
 * |ticks * baud - tick_hz|, which orders them as |ticks / bit_time - 1| does. */
static size_t nearest_baud(uint32_t ticks, uint32_t tick_hz)
{
    size_t best = 0;
    uint64_t best_d = UINT64_MAX;
    for (size_t i = 0; i < LAB_UART_BAUDS; i++) {
        uint64_t d = abs_diff((uint64_t)ticks * stark_lab_uart_baud(i), tick_hz);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

/* One pass of the refinement: the mean bit time of every pulse up to RUN_BITS long, each taken as
 * the nearest whole number of the current bit time. */
static uint32_t refine(const uint32_t *ts, size_t n, uint32_t glitch, uint32_t bit)
{
    uint64_t sum_w = 0;
    uint64_t sum_k = 0;
    for (size_t i = 0; i + 1u < n; i++) {
        uint32_t w = ts[i + 1u] - ts[i];
        if (w < glitch || w > (uint64_t)LAB_AUTOBAUD_RUN_BITS * bit) {
            continue;
        }
        uint64_t k = ((uint64_t)w + bit / 2u) / bit;
        sum_w += w;
        sum_k += k > 0u ? k : 1u;
    }
    /* NOTE: the shortest pulse itself qualifies (it is within 35 % of a bit time that is under
     * RUN_BITS bits long), so sum_k is never zero. */
    uint32_t mean = (uint32_t)((sum_w + sum_k / 2u) / sum_k);
    return mean > 0u ? mean : 1u; /* never a zero divisor for the next round */
}

void lab_autobaud_compute(const uint32_t *ts, size_t n, uint32_t tick_hz, stark_lab_autobaud_t *out)
{
    *out = (stark_lab_autobaud_t){.status = STARK_LAB_BAUD_INVALID, .edges = (uint32_t)n};
    if (tick_hz == 0u || n > LAB_AUTOBAUD_MAX_EDGES || (n > 0u && ts == NULL)) {
        return;
    }
    if (n < LAB_AUTOBAUD_MIN_EDGES) {
        out->status = STARK_LAB_BAUD_TOO_FEW;
        return;
    }
    uint32_t glitch = tick_hz / 1000000u; /* a pulse shorter than 1 us */
    if (glitch == 0u) {
        glitch = 1u; /* a clock under 1 MHz: only a zero-length step is a glitch */
    }
    uint32_t shortest = UINT32_MAX;
    for (size_t i = 0; i + 1u < n; i++) {
        uint32_t w = ts[i + 1u] - ts[i];
        if (w >= STEP_BACKWARDS) {
            return;
        }
        if (w >= glitch && w < shortest) {
            shortest = w;
        }
    }
    if (shortest == UINT32_MAX) {
        out->status = STARK_LAB_BAUD_NO_PULSE;
        return;
    }
    out->status = STARK_LAB_BAUD_UNKNOWN;
    size_t nearest = nearest_baud(shortest, tick_hz);
    uint32_t nearest_hz = stark_lab_uart_baud(nearest);
    out->raw_baud = (uint32_t)(((uint64_t)tick_hz + shortest / 2u) / shortest);
    if (abs_diff((uint64_t)shortest * nearest_hz, tick_hz) * 100u >
        (uint64_t)LAB_AUTOBAUD_NEAR_PCT * tick_hz) {
        return; /* the shortest pulse is near no standard bit time */
    }
    uint32_t bit = (uint32_t)(((uint64_t)tick_hz + nearest_hz / 2u) / nearest_hz);
    for (uint32_t round = 0; round < REFINE_ROUNDS; round++) {
        bit = refine(ts, n, glitch, bit);
    }
    out->bit_ticks = bit;
    out->raw_baud = (uint32_t)(((uint64_t)tick_hz + bit / 2u) / bit);
    for (size_t i = 0; i < LAB_UART_BAUDS; i++) {
        uint32_t b = stark_lab_uart_baud(i);
        if (abs_diff(out->raw_baud, b) * 100u <= (uint64_t)LAB_AUTOBAUD_SNAP_PCT * b) {
            out->status = STARK_LAB_BAUD_OK;
            out->baud = b;
            return;
        }
    }
}

const char *stark_lab_autobaud_text(const stark_lab_autobaud_t *r, char *buf, size_t n)
{
    switch (r->status) {
        case STARK_LAB_BAUD_OK:
            (void)snprintf(buf, n, "%u", (unsigned)r->baud);
            return buf;
        case STARK_LAB_BAUD_TOO_FEW:
            return "too few edges";
        case STARK_LAB_BAUD_NO_PULSE:
            return "no pulse";
        case STARK_LAB_BAUD_INVALID:
            return "invalid";
        case STARK_LAB_BAUD_UNKNOWN:
        default:
            return "unknown";
    }
}
