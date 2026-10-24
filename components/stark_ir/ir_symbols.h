/*
 * ir_symbols.h — private to stark_ir: RMT symbol words <-> timings
 * (TASKS.md STARK-0303/0304). Pure: a symbol is the 32-bit word of ESP-IDF's
 * rmt_symbol_word_t (duration0:15, level0:1, duration1:15, level1:1), read
 * here by bit position so no ESP-IDF header is needed.
 *
 * Received timings are level-agnostic: the first level seen is the mark.
 * A TSOP-class receiver's output is active low while the emulator's
 * loopback delivers the transmitter's active-high baseband (ADR-0019 §2);
 * both decode the same.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ir_types.h"

#define IR_SYMBOL_MAX_DURATION 32767u /* one half of a symbol, in ticks (1 tick = 1 us) */

/*
 * Words -> alternating mark/space timings: same-level neighbours merge, a
 * zero duration ends the list (the RMT end marker). false when the result
 * would exceed IR_MAX_DURATIONS or a merged duration exceeds 65535 us.
 */
bool ir_symbols_to_timings(const uint32_t *words, size_t n, ir_timings_t *out);

/*
 * Timings -> words for RMT TX: marks at level 1, spaces at level 0; a
 * duration above IR_SYMBOL_MAX_DURATION is split across halves. *n = words
 * written; false when they would exceed max.
 */
bool ir_timings_to_symbols(const ir_timings_t *t, uint32_t *words, size_t max, size_t *n);
