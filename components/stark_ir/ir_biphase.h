/*
 * ir_biphase.h — private to stark_ir: bi-phase (Manchester) helpers shared
 * by RC5 and RC6 (TASKS.md STARK-0302). Pure.
 *
 * A bi-phase frame is a sequence of half-bit levels (1 = mark). Captured
 * timings merge equal neighbours, so each duration is a whole number of
 * half-bit units.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ir_types.h"

#define IR_BIPHASE_MAX_HALVES 64

/*
 * Expands timings[from..] into half-bit levels, starting with `first_level`,
 * each duration classified as k units (1 <= k <= max_k): k = round(d / unit),
 * then d must match k * unit within IR_TOL_PCT. false when a duration does
 * not, or the result would exceed `cap`. *count = halves written.
 */
bool ir_biphase_expand(const ir_timings_t *t, size_t from, uint32_t unit_us, unsigned max_k,
                       uint8_t first_level, uint8_t *halves, size_t cap, size_t *count);

/* Merges half-bit levels into timings (unit_us each run unit), appending to
 * out; leading spaces are skipped (an emitter starts with a mark) and a
 * trailing space is dropped (it is invisible to a receiver). */
void ir_biphase_emit(const uint8_t *halves, size_t count, uint32_t unit_us, ir_timings_t *out);
