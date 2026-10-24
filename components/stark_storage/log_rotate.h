/*
 * log_rotate.h — private to stark_storage: when and how the log file
 * rotates (TASKS.md STARK-0206). Pure; host-tested at 100 % of lines.
 *
 * Files are stark.log (index 0, the one appended to) and stark.<i>.log
 * (i = 1 .. files-1, older as i grows).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define LOG_ROTATE_MAX_FILES 8

typedef struct {
    int from; /* index of the file to move */
    int to;   /* its new index, or -1: delete it */
} log_rotate_step_t;

/* Steps to run before appending `add` bytes to a stark.log of `size` bytes
 * so that it stays within `limit`: none when it fits (or the file is empty
 * — a single oversized append is never split). Otherwise delete the oldest,
 * shift the rest up by one and move stark.log to stark.1.log (files == 1:
 * just delete stark.log). Returns the step count (<= files). */
size_t log_rotate_plan(uint32_t size, uint32_t add, uint32_t limit, unsigned files,
                       log_rotate_step_t *out, size_t max);

/* "stark.log" for 0, "stark.<i>.log" otherwise; NUL-terminated. */
void log_rotate_name(int index, char *out, size_t len);
