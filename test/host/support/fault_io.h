/*
 * fault_io.h — host test support (TASKS.md STARK-0203): a storage_io_t that
 * forwards to storage_io_posix and fails on command, so every step of the
 * file helpers can be made to fail as a pulled or full card would.
 */
#pragma once

#include <stdbool.h>
#include "storage_io.h"

typedef enum {
    FAULT_OPEN,
    FAULT_READ,
    FAULT_WRITE,
    FAULT_FSYNC,
    FAULT_CLOSE,
    FAULT_RENAME,
    FAULT_UNLINK,
    FAULT_MKDIR,
    FAULT_LIST,
    FAULT_OPS
} fault_op_t;

extern const storage_io_t fault_io;

void fault_io_reset(void);
/* The nth call (1-based) of op from now fails with err; 0 = never. */
void fault_io_fail_nth(fault_op_t op, int nth, int err);
/* Every write returns at most `max` bytes (0 = unlimited). */
void fault_io_short_writes(size_t max);
/* Writes return 0 bytes (FAT's way to say the volume is full). */
void fault_io_zero_writes(bool on);
/* Calls of op so far (since reset). */
int fault_io_calls(fault_op_t op);
