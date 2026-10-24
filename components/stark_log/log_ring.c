/*
 * log_ring.c — the capture ring (log_ring.h). Pure.
 */
#include "log_ring.h"

static char at(const log_ring_t *r, size_t i)
{
    return r->buf[(r->head + i) % r->cap];
}

/* Drops the oldest line (through its newline, or everything if none). */
static void drop_line(log_ring_t *r)
{
    size_t n = 0;
    while (n < r->len && at(r, n) != '\n') {
        n++;
    }
    n = n < r->len ? n + 1 : r->len;
    r->head = (r->head + n) % r->cap;
    r->len -= n;
    r->dropped++;
}

void log_ring_init(log_ring_t *r, char *buf, size_t cap)
{
    r->buf = buf;
    r->cap = cap;
    r->head = 0;
    r->len = 0;
    r->dropped = 0;
}

void log_ring_put(log_ring_t *r, const char *s, size_t n)
{
    if (r->cap == 0) {
        return;
    }
    if (n > r->cap) {
        s += n - r->cap;
        n = r->cap;
    }
    while (r->cap - r->len < n) {
        drop_line(r);
    }
    for (size_t i = 0; i < n; i++) {
        r->buf[(r->head + r->len + i) % r->cap] = s[i];
    }
    r->len += n;
}

size_t log_ring_take(log_ring_t *r, char *out, size_t max, uint32_t *dropped)
{
    if (dropped != NULL) {
        *dropped = r->dropped;
    }
    r->dropped = 0;
    size_t limit = r->len < max ? r->len : max;
    size_t end = 0; /* bytes through the last newline within limit */
    for (size_t i = 0; i < limit; i++) {
        if (at(r, i) == '\n') {
            end = i + 1;
        }
    }
    if (end == 0 && limit == max) {
        end = limit; /* one line longer than out: move a piece */
    }
    for (size_t i = 0; i < end; i++) {
        out[i] = at(r, i);
    }
    r->head = (r->head + end) % r->cap;
    r->len -= end;
    return end;
}

size_t log_ring_used(const log_ring_t *r)
{
    return r->len;
}
