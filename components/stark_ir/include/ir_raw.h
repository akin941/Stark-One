/*
 * ir_raw.h — the `.ir` text file format (TASKS.md STARK-0302,
 * docs/ir-format.md). Pure; host-tested and fuzzed.
 *
 *   stark-ir 1
 *   protocol NEC
 *   address 0x0010
 *   command 0x0012
 *   carrier 38000
 *   timings 9000 4500 560 1690 ... 560
 *
 * The timings are what a replay sends; the other fields are informational
 * and re-derived from the timings when a file is read.
 */
#pragma once

#include <stddef.h>
#include "ir_types.h"
#include "stark_err.h"

#define IR_RAW_TEXT_MAX 1800 /* a full 256-duration file fits */

/* Writes the file text for s into out (NUL-terminated). Returns the length,
 * or 0 when out is too small (nothing useful written). */
size_t ir_raw_format(const ir_signal_t *s, char *out, size_t len);

/*
 * Reads len bytes of file text (no NUL needed). STARK_ERR_INVALID_ARG when
 * the first line is not `stark-ir 1`, `timings` is missing or holds a value
 * of 0, above 65535, non-numeric, or more than IR_MAX_DURATIONS values, the
 * carrier is outside 20000..60000 Hz, or a line is longer than the buffer
 * this parser allows. Unknown keys and `#` comments are ignored. out->frame
 * is decoded from the timings; a missing carrier takes the protocol's
 * default (38000 for raw).
 */
stark_err_t ir_raw_parse(const char *text, size_t len, ir_signal_t *out);
