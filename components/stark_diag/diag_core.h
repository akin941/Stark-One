/*
 * diag_core.h — private to stark_diag: the pure arithmetic and formatting
 * of the diagnostics line (TASKS.md STARK-0109). Host-tested.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_diag.h"

/*
 * Frames per second x 10 between two samples of a free-running frame
 * counter. Both the counter and the clock may wrap (unsigned subtraction);
 * zero elapsed time gives 0. Rounded to the nearest tenth, saturating.
 */
uint32_t diag_fps_x10(uint32_t frames_now, uint32_t frames_prev, uint64_t now_us, uint64_t prev_us);

/*
 * Writes "heap=<n> min=<n> fps=<n.n> drops=<n> overruns=<n>" (the `diag:`
 * line's payload; heap= first, which the scenario runners parse) into buf,
 * always NUL-terminated when len > 0. Returns what snprintf returns: the
 * full length, even when truncated.
 */
int diag_format(const stark_diag_snapshot_t *s, char *buf, size_t len);
