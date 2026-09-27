/*
 * stark_event_core.h — L2 core: the event bus as a pure data structure
 * (ARCHITECTURE.md §6.3, TASKS.md STARK-0009).
 *
 * A fixed-capacity ring of stark_event_t in caller-provided storage, plus
 * a table of up to STARK_EVENT_CORE_MAX_SUBS subscribers, each with a type
 * mask. No allocation, no OS: every operation brackets its access to the
 * bus with the injected lock, and the lock is never held while a handler
 * runs — so a handler may publish or subscribe even when the lock is a
 * non-recursive mutex. Host tests inject a no-op lock.
 *
 * stark_event_handler_t and stark_event_stats_t are declared in
 * stark_event.h, which the global bus's API (STARK-0010) shares.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"
#include "stark_event.h"

#define STARK_EVENT_CORE_MAX_SUBS 8

/* Both functions are required; ctx is passed through untouched. */
typedef struct {
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
    void *ctx;
} stark_lock_t;

typedef struct {
    uint32_t mask;
    stark_event_handler_t fn;
    void *ctx;
} stark_event_sub_t;

/*
 * Caller-allocated (it must be a complete type for that), but every field
 * is private to stark_event_core.c except `stats`, which the bus owner may
 * read while holding the lock it injected.
 */
typedef struct {
    stark_event_t *storage;
    size_t capacity;
    size_t head;  /* index of the oldest queued event */
    size_t count; /* queued events */
    stark_lock_t lock;
    stark_event_sub_t subs[STARK_EVENT_CORE_MAX_SUBS];
    size_t sub_count;
    bool dispatching;
    stark_event_stats_t stats;
} stark_event_core_t;

/*
 * Prepares `bus` to use storage[0 .. capacity-1] as its ring: empty, no
 * subscribers, stats zeroed. Calling it again resets the bus the same way.
 * STARK_ERR_INVALID_ARG if bus or storage is NULL, capacity is 0, or either
 * lock function is NULL.
 */
stark_err_t stark_event_core_init(stark_event_core_t *bus, stark_event_t *storage, size_t capacity,
                                  stark_lock_t lock);

/*
 * Copies *e onto the ring. If the ring is full, the oldest queued event is
 * discarded and stats.dropped incremented — the new event is still
 * accepted and STARK_OK returned. STARK_ERR_INVALID_ARG (bus unchanged) if
 * bus or e is NULL, or e->type is not STARK_EVT_NONE < type <=
 * STARK_EVT_TYPE_LAST.
 */
stark_err_t stark_event_core_publish(stark_event_core_t *bus, const stark_event_t *e);

/*
 * Delivers up to max_events queued events, oldest first. Each event goes
 * to every subscriber whose mask has its bit, in subscription order; an
 * event no one subscribes to is still consumed. Only events already queued
 * when dispatch() starts are eligible: anything published during the call
 * (from a handler or elsewhere) waits for the next call. A subscriber
 * added during the call receives events from the next one dequeued
 * onwards. Never recurses: a dispatch() started while another is running
 * on the same bus returns 0 and delivers nothing.
 *
 * Returns the number of events consumed — 0 for a NULL bus, an empty ring
 * or max_events == 0.
 */
size_t stark_event_core_dispatch(stark_event_core_t *bus, uint32_t max_events);

/*
 * Adds fn (with ctx) for every event type whose bit is set in type_mask
 * (see STARK_EVT_MASK). STARK_ERR_INVALID_ARG if bus or fn is NULL or
 * type_mask is 0; STARK_ERR_NO_MEM if all STARK_EVENT_CORE_MAX_SUBS slots
 * are taken.
 */
stark_err_t stark_event_core_subscribe(stark_event_core_t *bus, uint32_t type_mask,
                                       stark_event_handler_t fn, void *ctx);
