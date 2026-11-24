/*
 * lab_meas.h — frequency and duty from timestamped edges (TASKS.md STARK-0703, ADR-0023 §4).
 * Pure: no lock, no allocation, no ESP-IDF header, a bounded loop over at most
 * STARK_LAB_MEAS_RING timestamps. The result types are stark_lab.h's (the app reads them);
 * stark_lab.c and lab_pins.c bind the window to the transport, the host tests bind it to
 * synthetic edge sets (test/fake/fake_lab_vectors.h, from scripts/gen_lab_vectors.py, an
 * independent Python implementation of the same definitions).
 *
 * The definitions (the Python reference restates them from here, not from the C):
 *  - `ts` are the CPU cycle counts of every edge in order, `first_level` the line's level
 *    before the first one, so edge k leads to !first_level for even k and to first_level for odd.
 *  - A step between two consecutive stamps is the unsigned 32-bit difference, so a counter
 *    that wrapped inside the window is harmless; a step of 2^31 or more is a stamp that went
 *    backwards (INVALID).
 *  - A pulse (the time between two edges) shorter than 1 us, a zero step included, is a GLITCH.
 *  - Frequency = (rising edges - 1) / (first rising edge to the last), rounded to 0.01 Hz;
 *    duty = the sum of the complete high intervals over the sum of the complete periods they
 *    sit in (rising edge to the next rising edge), rounded to 0.1 %. At least two rising edges
 *    are needed, else TOO_SLOW.
 *  - The measured range is 1 Hz to 50 kHz, with the limits themselves in range: a result is
 *    TOO_FAST above 50 kHz + 0.1 % and TOO_SLOW below 0.99 Hz, so a 50 kHz signal read with its
 *    own jitter is never "too fast".
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_lab.h"

typedef stark_lab_meas_t lab_meas_t;

#define LAB_MEAS_GATE_MS       20u   /* the pre-gate that only counts edges */
#define LAB_MEAS_WINDOW_MS_MIN 10u   /* the capture window */
#define LAB_MEAS_WINDOW_MS_MAX 4000u /* far inside the counter's 17.9 s wrap */
#define LAB_MEAS_TARGET_EDGES  768u  /* the window is sized for about this many of the 1024 */
#define LAB_MEAS_GLITCH_US     1u
#define LAB_MEAS_HZ_X100_MAX   5005000u /* 50 kHz + 0.1 % */
#define LAB_MEAS_HZ_X100_MIN   99u      /* 1 Hz less one count of 0.01 Hz */

/*
 * The result for one captured window. `overflowed` (more edges than the ring held) is reported
 * as OVERFLOW whatever the stamps say; `tick_hz` is the clock the stamps count at (0: INVALID);
 * no stamp at all is NO_SIGNAL with the line's level in steady_high. n above
 * STARK_LAB_MEAS_RING or a NULL `ts` with n > 0 is INVALID.
 */
lab_meas_t lab_meas_compute(const uint32_t *ts, size_t n, bool first_level, uint32_t tick_hz,
                            bool overflowed);

typedef struct {
    bool too_fast;      /* the gate saw more than 50 kHz: no capture is made */
    uint32_t window_ms; /* LAB_MEAS_WINDOW_MS_MIN..MAX, sized so the expected edges fit the ring */
} lab_meas_plan_t;

/*
 * The capture window for `gate_edges` edges seen in LAB_MEAS_GATE_MS: about
 * LAB_MEAS_TARGET_EDGES of them, 10 ms at least and 4 s at most, and 4 s when nothing arrived.
 * An estimate above 50 kHz + 0.1 % is `too_fast`.
 */
lab_meas_plan_t lab_meas_plan(uint32_t gate_edges);
