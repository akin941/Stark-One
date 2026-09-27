/*
 * stark_event.h — L2, the event type every producer and consumer shares
 * (ARCHITECTURE.md §6.3).
 *
 * Pure: no ESP-IDF, no FreeRTOS — host-includable. The bus data structure
 * is stark_event_core.h; the one global, FreeRTOS-backed bus is the port's
 * job (STARK-0010), not this header's.
 */
#pragma once

#include <stdint.h>

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
 * writes it (the port stamps it when left zero, STARK-0010).
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
