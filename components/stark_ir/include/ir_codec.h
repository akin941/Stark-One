/*
 * ir_codec.h — encode and decode IR frames (TASKS.md STARK-0301/0302).
 * Pure; host-tested against spec-derived vectors (ADR-0019 §3).
 */
#pragma once

#include <stdint.h>
#include "ir_types.h"
#include "stark_err.h"

/* Timing tolerance used by every decoder: nominal +/- max(25 %, 60 us). */
#define IR_TOL_PCT 25

/*
 * Nominal timings for a frame, and its carrier (NEC/NECX 38 kHz). A field
 * that does not fit the protocol (an NEC address above 0xFF, ...) or an
 * unsupported protocol is STARK_ERR_INVALID_ARG.
 */
stark_err_t ir_encode(const ir_frame_t *f, ir_timings_t *out, uint32_t *carrier_hz);

/* The first protocol, in a fixed order, that matches the timings
 * completely; IR_PROTO_UNKNOWN (and *out zeroed) when none does. */
ir_protocol_t ir_decode(const ir_timings_t *t, ir_frame_t *out);

/* "NEC", "NECX", "RC5", "RC6", "SIRC12", "SIRC15", "SIRC20", "RAW". */
const char *ir_protocol_name(ir_protocol_t p);
