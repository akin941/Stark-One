/*
 * ir_timing.h — private to stark_ir: duration matching and building
 * (TASKS.md STARK-0301). Pure.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "ir_types.h"

#define IR_TOL_FLOOR_US 60 /* receivers quantise short marks coarser than a percentage */

/* measured within nominal +/- max(tol_pct %, IR_TOL_FLOOR_US) (inclusive). */
bool ir_match(uint32_t measured_us, uint32_t nominal_us, uint8_t tol_pct);

/* Appends one duration; false (and nothing changed) when t is full. */
bool ir_push(ir_timings_t *t, uint32_t us);
