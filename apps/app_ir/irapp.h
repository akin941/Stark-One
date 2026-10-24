/*
 * irapp.h — private to the IR app (TASKS.md STARK-0305/0306).
 */
#pragma once

#include "ir_types.h"

#define IRAPP_RECENT 6 /* captures shown, newest first */

/* Saves s as the next captures/ir_NNNN.ir on the app worker (ir_save.c).
 * Logs `irapp: saved <name>`, `irapp: save failed: <err>` or
 * `irapp: save refused: <why>`. */
void irapp_save(const ir_signal_t *s);
