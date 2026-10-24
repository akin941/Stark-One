/*
 * log_rotate.c — rotation plan and names (log_rotate.h). Pure.
 */
#include "log_rotate.h"
#include <stdio.h>

size_t log_rotate_plan(uint32_t size, uint32_t add, uint32_t limit, unsigned files,
                       log_rotate_step_t *out, size_t max)
{
    if (size == 0 || (uint64_t)size + add <= limit || files == 0 || max < files) {
        return 0;
    }
    size_t n = 0;
    out[n++] = (log_rotate_step_t){(int)files - 1, -1}; /* the oldest goes */
    for (int i = (int)files - 2; i >= 0; i--) {
        out[n++] = (log_rotate_step_t){i, i + 1};
    }
    return n;
}

void log_rotate_name(int index, char *out, size_t len)
{
    if (index == 0) {
        (void)snprintf(out, len, "stark.log");
    } else {
        (void)snprintf(out, len, "stark.%d.log", index);
    }
}
