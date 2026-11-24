/*
 * lab_autobaud.h — auto-baud detection from timestamped edges (TASKS.md STARK-0705, ADR-0023 §7).
 * Pure: the caller captures the edges of the RX line (stark_lab_transport_pad_edges(), the stamps
 * of the CPU cycle counter) and this turns them into a baud from the standard list or "unknown".
 * The result types and the text are the public ones of stark_lab.h.
 *
 * The rule, in whole numbers: the pulse widths are the differences between consecutive stamps;
 * pulses under 1 us are glitches and are ignored; the shortest of the rest is the one-bit pulse,
 * and the standard bit time nearest to it (within 35 %) is where the estimate starts; the bit time
 * is then refined as the total time of all pulses up to twelve bits long divided by the total
 * number of bits they hold (each width rounded to a whole number of bits, twice), which averages
 * edge jitter out instead of trusting the one pulse that jitter shortened most; the baud that
 * bit time gives is snapped to the list within 3 %, else "unknown". A baud is never invented: a
 * line with fewer than 16 edges, no pulse over 1 us, or a bit time near no standard baud is a
 * status and no number.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_lab.h"

#define LAB_AUTOBAUD_MIN_EDGES 16u
#define LAB_AUTOBAUD_MAX_EDGES 2048u
#define LAB_AUTOBAUD_SNAP_PCT  3u  /* the estimate must be this close to a listed baud */
#define LAB_AUTOBAUD_NEAR_PCT  35u /* the shortest pulse must be this close to a listed bit time */
#define LAB_AUTOBAUD_RUN_BITS  12u /* longer pulses are idle time, not data */

/* `ts` holds `n` stamps (any edge, rising or falling) in a clock of `tick_hz` hertz. */
void lab_autobaud_compute(const uint32_t *ts, size_t n, uint32_t tick_hz,
                          stark_lab_autobaud_t *out);
