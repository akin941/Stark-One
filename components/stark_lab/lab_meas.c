/*
 * lab_meas.c — frequency and duty from timestamped edges (lab_meas.h). Pure, bounded: one pass
 * over at most STARK_LAB_MEAS_RING stamps, 64-bit sums that cannot overflow (1024 steps below
 * 2^31 each).
 */
#include "lab_meas.h"
#include <stdio.h>

#define STEP_FORWARD_MAX 0x7FFFFFFFu /* a larger unsigned step is a stamp that went backwards */

static lab_meas_t make(stark_lab_meas_status_t status, size_t n)
{
    lab_meas_t r = {.status = status};
    r.edges = (uint32_t)n;
    return r;
}

lab_meas_t lab_meas_compute(const uint32_t *ts, size_t n, bool first_level, uint32_t tick_hz,
                            bool overflowed)
{
    if (tick_hz == 0u) {
        return make(STARK_LAB_MEAS_INVALID, 0);
    }
    if (overflowed) {
        return make(STARK_LAB_MEAS_OVERFLOW, n > STARK_LAB_MEAS_RING ? STARK_LAB_MEAS_RING : n);
    }
    if (n == 0u) {
        lab_meas_t r = make(STARK_LAB_MEAS_NO_SIGNAL, 0);
        r.steady_high = first_level;
        return r;
    }
    if (ts == NULL || n > STARK_LAB_MEAS_RING) {
        return make(STARK_LAB_MEAS_INVALID, 0);
    }

    /* Pass 1: every step must go forward, and no pulse may be shorter than the glitch limit. */
    bool glitch = false;
    for (size_t i = 1; i < n; i++) {
        uint32_t step = ts[i] - ts[i - 1u];
        if (step > STEP_FORWARD_MAX) {
            return make(STARK_LAB_MEAS_INVALID, n);
        }
        if ((uint64_t)step * 1000000u < (uint64_t)tick_hz * LAB_MEAS_GLITCH_US) {
            glitch = true;
        }
    }
    if (glitch) {
        return make(STARK_LAB_MEAS_GLITCH, n);
    }

    /* Pass 2: the rising edges are every second edge from index `first`. */
    const size_t first = first_level ? 1u : 0u;
    const size_t rises = n > first ? (n - first + 1u) / 2u : 0u;
    if (rises < 2u) {
        return make(STARK_LAB_MEAS_TOO_SLOW, n);
    }
    uint64_t pos = 0;      /* ts[i] - ts[0] */
    uint64_t rise_at = 0;  /* the latest rising edge */
    uint64_t fall_at = 0;  /* the latest falling edge after it */
    uint64_t first_at = 0; /* the first rising edge */
    uint64_t high = 0;     /* the complete high intervals so far */
    size_t seen = 0;
    for (size_t i = 0; i < n; i++) {
        if (i > 0u) {
            pos += (uint32_t)(ts[i] - ts[i - 1u]);
        }
        if (i >= first && ((i - first) & 1u) == 0u) {
            if (seen == 0u) {
                first_at = pos;
            } else {
                high += fall_at - rise_at; /* the period that this rising edge closes */
            }
            rise_at = pos;
            seen++;
        } else if (seen > 0u) {
            fall_at = pos;
        }
    }
    const uint64_t span = rise_at - first_at; /* > 0: every step is at least one tick */
    const uint64_t periods = rises - 1u;
    const uint64_t hz_x100 = (periods * tick_hz * 100u + span / 2u) / span;
    if (hz_x100 > LAB_MEAS_HZ_X100_MAX) {
        return make(STARK_LAB_MEAS_TOO_FAST, n);
    }
    if (hz_x100 < LAB_MEAS_HZ_X100_MIN) {
        return make(STARK_LAB_MEAS_TOO_SLOW, n);
    }
    lab_meas_t r = make(STARK_LAB_MEAS_OK, n);
    r.hz_x100 = (uint32_t)hz_x100;
    r.period_ticks = (uint32_t)((span + periods / 2u) / periods);
    r.duty_x10 = (uint16_t)((high * 1000u + span / 2u) / span);
    return r;
}

lab_meas_plan_t lab_meas_plan(uint32_t gate_edges)
{
    lab_meas_plan_t p = {.too_fast = false, .window_ms = LAB_MEAS_WINDOW_MS_MAX};
    if (gate_edges == 0u) {
        return p;
    }
    /* edges in the gate -> periods per second: edges / 2 / gate seconds, in 0.01 Hz units */
    uint64_t est_x100 = (uint64_t)gate_edges * (1000u / LAB_MEAS_GATE_MS) * 100u / 2u;
    if (est_x100 > LAB_MEAS_HZ_X100_MAX) {
        p.too_fast = true;
        p.window_ms = 0;
        return p;
    }
    uint32_t ms = (LAB_MEAS_TARGET_EDGES * LAB_MEAS_GATE_MS) / gate_edges;
    if (ms < LAB_MEAS_WINDOW_MS_MIN) {
        ms = LAB_MEAS_WINDOW_MS_MIN;
    }
    if (ms > LAB_MEAS_WINDOW_MS_MAX) {
        ms = LAB_MEAS_WINDOW_MS_MAX;
    }
    p.window_ms = ms;
    return p;
}

/* ---- text ---------------------------------------------------------------------------------- */

static const char *steady_text(const lab_meas_t *m)
{
    return m->steady_high ? "no signal (high)" : "no signal (low)";
}

void stark_lab_meas_text(const stark_lab_meas_t *m, char *buf, size_t n)
{
    switch (m->status) {
        case STARK_LAB_MEAS_OK: {
            unsigned duty = m->duty_x10;
            if (m->hz_x100 >= 100000u) {
                (void)snprintf(buf, n, "%u.%03u kHz duty %u.%u %%",
                               (unsigned)(m->hz_x100 / 100000u),
                               (unsigned)((m->hz_x100 % 100000u) / 100u), duty / 10u, duty % 10u);
            } else {
                (void)snprintf(buf, n, "%u.%02u Hz duty %u.%u %%", (unsigned)(m->hz_x100 / 100u),
                               (unsigned)(m->hz_x100 % 100u), duty / 10u, duty % 10u);
            }
            break;
        }
        case STARK_LAB_MEAS_TOO_FAST:
            (void)snprintf(buf, n, "> 50 kHz, not measured");
            break;
        case STARK_LAB_MEAS_TOO_SLOW:
            (void)snprintf(buf, n, "< 1 Hz");
            break;
        case STARK_LAB_MEAS_NO_SIGNAL:
            (void)snprintf(buf, n, "%s", steady_text(m));
            break;
        case STARK_LAB_MEAS_OVERFLOW:
            (void)snprintf(buf, n, "overflow: too many edges");
            break;
        case STARK_LAB_MEAS_GLITCH:
            (void)snprintf(buf, n, "glitches (< 1 us)");
            break;
        case STARK_LAB_MEAS_INVALID:
        default:
            (void)snprintf(buf, n, "invalid timestamps");
            break;
    }
}

void stark_lab_meas_log_text(const stark_lab_meas_t *m, char *buf, size_t n)
{
    switch (m->status) {
        case STARK_LAB_MEAS_OK: {
            unsigned tenth = (unsigned)((m->hz_x100 + 5u) / 10u); /* 0.1 Hz units, rounded */
            unsigned duty = m->duty_x10;
            (void)snprintf(buf, n, "%u.%u Hz duty %u.%u %%", tenth / 10u, tenth % 10u, duty / 10u,
                           duty % 10u);
            break;
        }
        case STARK_LAB_MEAS_TOO_FAST:
            (void)snprintf(buf, n, "too fast");
            break;
        case STARK_LAB_MEAS_TOO_SLOW:
            (void)snprintf(buf, n, "too slow");
            break;
        case STARK_LAB_MEAS_NO_SIGNAL:
            (void)snprintf(buf, n, "%s", steady_text(m));
            break;
        case STARK_LAB_MEAS_OVERFLOW:
            (void)snprintf(buf, n, "overflow");
            break;
        case STARK_LAB_MEAS_GLITCH:
            (void)snprintf(buf, n, "glitch");
            break;
        case STARK_LAB_MEAS_INVALID:
        default:
            (void)snprintf(buf, n, "invalid");
            break;
    }
}
