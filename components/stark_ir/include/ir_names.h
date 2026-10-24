/*
 * ir_names.h — capture file names (TASKS.md STARK-0305): `ir_NNNN.ir` in
 * captures/, numbered upwards, and the order Browse lists them in
 * (STARK-0306). Pure; host-tested.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

#define IR_NAME_MAX 10000u /* ir_0001.ir .. ir_9999.ir */

typedef struct {
    uint32_t highest; /* largest NNNN seen, 0 for none */
} ir_names_t;

void ir_names_init(ir_names_t *st);

/* Feeds one directory entry; names that are not `ir_NNNN.ir` are ignored.
 * Returns true when it was a capture name. */
bool ir_names_scan(ir_names_t *st, const char *name);

/* "ir_<highest + 1>.ir"; STARK_ERR_NO_SPACE once ir_9999.ir exists,
 * STARK_ERR_INVALID_ARG for a buffer under 11 bytes. */
stark_err_t ir_names_next(const ir_names_t *st, char *out, size_t len);

/* Browse order, for qsort-style use: saved captures (`ir_NNNN.ir`) first,
 * newest first; every other name after them, by name. */
int ir_names_compare(const char *a, const char *b);
