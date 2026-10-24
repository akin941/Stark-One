/*
 * storage_core.h — private to stark_storage: the pure mount state machine
 * (TASKS.md STARK-0201). No libc beyond <stdint.h>/<stdbool.h>, no ESP-IDF;
 * host-tested at 100 % of lines.
 *
 * The port feeds it inputs and performs the action each step returns,
 * feeding that action's result back in as the next input:
 *
 *     step = storage_core_step(&c, STORAGE_IN_TICK, STARK_OK, now);
 *     while (step.action != STORAGE_ACT_NONE) { do it; step = ...(result) }
 *
 * Time is a free-running millisecond counter; wrap is handled.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stark_err.h"
#include "stark_storage.h"

typedef enum {
    STORAGE_IN_TICK,           /* periodic: runs whatever is due */
    STORAGE_IN_MOUNT_RESULT,   /* result of STORAGE_ACT_TRY_MOUNT */
    STORAGE_IN_PROBE_RESULT,   /* result of STORAGE_ACT_PROBE */
    STORAGE_IN_PRESENT_RESULT, /* result of STORAGE_ACT_PRESENT_CHECK */
    STORAGE_IN_IO_ERROR,       /* a file operation failed with an I/O error */
    STORAGE_IN_REQ_MOUNT,      /* the user asked to mount now */
    STORAGE_IN_REQ_EJECT,      /* the user asked to eject; the port checked no file is open */
} storage_input_t;

typedef enum {
    STORAGE_ACT_NONE,
    STORAGE_ACT_TRY_MOUNT,
    STORAGE_ACT_PROBE,
    STORAGE_ACT_PRESENT_CHECK, /* ejected: does a card still answer? (no mount) */
    STORAGE_ACT_UNMOUNT,       /* release the card: it went away, or eject */
} storage_action_t;

typedef struct {
    uint32_t retry_ms; /* mount attempts while no usable card is mounted */
    uint32_t probe_ms; /* presence probes while mounted */
} storage_timing_t;

typedef struct {
    stark_storage_state_t state;
    storage_timing_t timing;
    uint32_t due_ms;     /* when the next periodic action is due */
    bool due_now;        /* an action is due regardless of due_ms */
    bool announced;      /* the first state has been published */
    uint32_t generation; /* bumped whenever a mounted volume goes away (removal, eject) */
} storage_core_t;

typedef struct {
    storage_action_t action; /* to perform now */
    bool changed;            /* state changed: publish and log it */
} storage_step_t;

/* fitted == false: STARK_STORAGE_NOT_FITTED forever, every step a no-op.
 * Otherwise the first mount attempt is due immediately. */
void storage_core_init(storage_core_t *c, bool fitted, storage_timing_t timing, uint32_t now_ms);

/* result is only read for the *_RESULT inputs. */
storage_step_t storage_core_step(storage_core_t *c, storage_input_t in, stark_err_t result,
                                 uint32_t now_ms);

/* Milliseconds until the next periodic action is due (0 = now). A fitted
 * core always has one due; NOT_FITTED returns UINT32_MAX. */
uint32_t storage_core_wait_ms(const storage_core_t *c, uint32_t now_ms);
