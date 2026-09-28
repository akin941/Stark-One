/*
 * stark_diag.h — L3, diagnostics v1 (TASKS.md STARK-0109, ARCHITECTURE.md
 * §6.9): heap, task stack high-water marks, FPS, frame overruns and event
 * drops, measured in one place, logged, and shown by the Diagnostics app.
 * Local only — nothing leaves the device. Self-test and log export are V0.7.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

/* The UI's counters, which L3 cannot read itself (stark_ui/stark_app are
 * L4): main injects a source that fills them (stark_diag_init()). */
typedef struct {
    uint32_t frames;
    uint32_t overruns;
    uint32_t render_errors;
    uint32_t join_timeouts;
} stark_diag_ui_t;

typedef void (*stark_diag_ui_fn)(stark_diag_ui_t *out);

typedef struct {
    uint32_t heap_free;    /* free internal heap, bytes */
    uint32_t heap_min;     /* minimum ever free (the allocator tracks it) */
    uint32_t heap_largest; /* largest free block */
    uint32_t fps_x10;      /* frames per second x 10, over the last sampling window */
    stark_diag_ui_t ui;
    uint32_t ev_published;
    uint32_t ev_dropped;
    uint32_t ev_max_depth;
    uint32_t uptime_s;
} stark_diag_snapshot_t;

typedef struct {
    char name[16];
    uint32_t stack_free; /* high-water mark: the least free stack seen, bytes */
} stark_diag_task_t;

/* STARK_EVT_SYSTEM system.id published after every sample (once a second);
 * the Diagnostics app refreshes on it. */
#define STARK_SYS_DIAG_SAMPLE 1u

/*
 * Takes a first sample at once, then starts the 1 s sampler (an esp_timer).
 * Each sample computes FPS from the frame counter, refreshes the snapshot
 * and publishes STARK_SYS_DIAG_SAMPLE; the first one and then one every
 * CONFIG_STARK_DIAG_LOG_PERIOD_S (10; 0 = never) also log the line
 *     diag: heap=<n> min=<n> fps=<n.n> drops=<n> overruns=<n>
 * ui_source may be NULL (UI counters stay 0). Needs stark_event_init().
 * STARK_ERR_STATE if already started.
 */
stark_err_t stark_diag_init(stark_diag_ui_fn ui_source);

/* The latest sample (any task). STARK_ERR_INVALID_ARG for NULL,
 * STARK_ERR_STATE before init. */
stark_err_t stark_diag_snapshot(stark_diag_snapshot_t *out);

/* Name and stack high-water mark of up to max tasks; returns how many
 * were written. */
size_t stark_diag_tasks(stark_diag_task_t *out, size_t max);
