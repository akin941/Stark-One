/*
 * stark_event.h — the event type every producer and consumer shares, and
 * the one global bus they use (ARCHITECTURE.md §6.3).
 *
 * No ESP-IDF or FreeRTOS types appear here, so the header stays
 * host-includable; the global bus itself is implemented only on target
 * (stark_event.c, TASKS.md STARK-0010). The bus data structure it wraps is
 * the pure core in stark_event_core.h.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

typedef enum {
    STARK_EVT_NONE = 0,
    STARK_EVT_KEY,         /* payload: key */
    STARK_EVT_TICK,        /* periodic, from UI loop */
    STARK_EVT_APP_REQUEST, /* launch/exit requests */
    STARK_EVT_SYSTEM,      /* low battery, sd inserted, module attached … */
} stark_evt_type_t;

/* The last valid event type. Update it when a type is appended — the bus
 * rejects anything outside STARK_EVT_NONE < type <= STARK_EVT_TYPE_LAST. */
#define STARK_EVT_TYPE_LAST  STARK_EVT_SYSTEM

/* Subscriber type-mask bit for one event type: one bit per type, so a
 * subscriber to KEY and SYSTEM passes
 * STARK_EVT_MASK(STARK_EVT_KEY) | STARK_EVT_MASK(STARK_EVT_SYSTEM). */
#define STARK_EVT_MASK(type) (UINT32_C(1) << (uint32_t)(type))

_Static_assert(STARK_EVT_TYPE_LAST < 32, "subscriber type masks are uint32_t");

/*
 * Plain value type: the bus copies it in on publish and hands handlers a
 * copy, so it carries no pointers and has no ownership rules. Only the
 * union member matching `type` is meaningful; STARK_EVT_TICK has no
 * payload. `ts_us` is the producer's to set — the core never reads or
 * writes it; stark_event_publish() stamps it when left zero.
 */
typedef struct {
    stark_evt_type_t type;
    uint64_t ts_us;
    union {
        struct {
            uint8_t key;
            uint8_t action;
            uint8_t repeat;
        } key;
        struct {
            uint32_t id;
            int32_t arg;
        } app;
        struct {
            uint32_t id;
            int32_t arg;
        } system;
    };
} stark_event_t;

/* `e` points at a copy owned by dispatch(), valid only for the call. */
typedef void (*stark_event_handler_t)(const stark_event_t *e, void *ctx);

typedef struct {
    uint32_t published; /* events accepted by publish(), dropped ones included */
    uint32_t dropped;   /* oldest events discarded because the ring was full */
    uint32_t max_depth; /* high-water mark of queued events */
} stark_event_stats_t;

/* ---- The global bus (STARK-0010) -----------------------------------------
 *
 * One bus for the whole firmware: static storage for
 * CONFIG_STARK_EVENT_QUEUE_LEN events, a FreeRTOS mutex injected into the
 * core as its lock, and a binary semaphore that publish() gives and wait()
 * takes. Ordering, overflow, handler and dispatch semantics are exactly
 * the core's (stark_event_core.h): handlers run on the dispatching task
 * with the mutex released, so they may publish or subscribe.
 *
 * Producers: any number of tasks may publish concurrently. Consumer:
 * exactly one task (the UI task) calls stark_event_wait() and
 * stark_event_dispatch(); a second consumer is not supported, and neither
 * may be called from inside a handler.
 *
 * Task context only — including esp_timer callbacks created with the
 * default ESP_TIMER_TASK dispatch method, which run in the esp_timer task;
 * never ESP_TIMER_ISR callbacks or any other ISR. V0 has no ISR producer
 * (TASKS.md STARK-0010). Before stark_event_init(), every call returns
 * STARK_ERR_STATE (dispatch() returns 0) and touches nothing.
 */

/*
 * Creates the bus. Call once, from app_main(), before any task that uses
 * the bus starts — it is not itself thread-safe. Allocates nothing: the
 * mutex and semaphore are static FreeRTOS objects. STARK_ERR_STATE if
 * already initialised.
 */
stark_err_t stark_event_init(void);

/*
 * Publishes a copy of *e — with ts_us set from stark_hal_now_us() if the
 * caller left it 0; a non-zero ts_us is kept as given — then wakes
 * stark_event_wait(). Errors as stark_event_core_publish().
 */
stark_err_t stark_event_publish(const stark_event_t *e);

/* As stark_event_core_subscribe(), on the global bus: STARK_OK,
 * STARK_ERR_INVALID_ARG, or STARK_ERR_NO_MEM when all 8 slots are taken. */
stark_err_t stark_event_subscribe(uint32_t type_mask, stark_event_handler_t fn, void *ctx);

/* As stark_event_core_dispatch(), on the global bus, run by the consumer
 * task: delivers up to max_events events queued before the call and
 * returns how many it consumed. */
size_t stark_event_dispatch(uint32_t max_events);

/*
 * Returns STARK_OK as soon as the bus holds at least one event not yet
 * dispatched — at once if one is already queued, including one a handler
 * published during the previous dispatch — otherwise blocks until one is
 * published. STARK_ERR_TIMEOUT if none arrives within timeout_ms (0 polls
 * without blocking). STARK_OK therefore always means dispatch() has work;
 * an empty bus never reports it. Timeouts have FreeRTOS tick granularity
 * (1 ms at CONFIG_FREERTOS_HZ=1000) and every value is finite: there is
 * no wait-forever.
 */
stark_err_t stark_event_wait(uint32_t timeout_ms);

/* Copies the core's counters into *out, taken under the bus mutex so the
 * three fields are mutually consistent. STARK_ERR_INVALID_ARG if out is
 * NULL. */
stark_err_t stark_event_stats(stark_event_stats_t *out);
