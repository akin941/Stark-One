/*
 * log_ring.h — private to stark_log: the capture ring (TASKS.md STARK-0206).
 * A byte ring of whole lines: when it is full the oldest lines go, whole,
 * and are counted. Pure; host-tested at 100 % of lines. Not thread-safe —
 * stark_log.c wraps every call in a spinlock.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *buf;
    size_t cap;
    size_t head;      /* oldest byte */
    size_t len;       /* bytes held */
    uint32_t dropped; /* whole lines lost since the last take */
} log_ring_t;

void log_ring_init(log_ring_t *r, char *buf, size_t cap);

/* Appends n bytes (a line or a piece of one). Text longer than the ring
 * keeps only its last cap bytes. Makes room by dropping whole oldest lines. */
void log_ring_put(log_ring_t *r, const char *s, size_t n);

/* Moves whole lines, oldest first, into out (up to max bytes) and returns
 * the byte count. A single line longer than max is moved in pieces so the
 * ring can never stall. *dropped (optional) gets the lines lost since the
 * last take, and the count restarts. */
size_t log_ring_take(log_ring_t *r, char *out, size_t max, uint32_t *dropped);

size_t log_ring_used(const log_ring_t *r);
