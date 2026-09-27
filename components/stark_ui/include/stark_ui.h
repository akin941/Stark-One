/*
 * stark_ui.h — L4, the screen stack and the UI loop (ARCHITECTURE.md §6.7,
 * TASKS.md STARK-0017).
 *
 * Retained-mode-lite: a screen keeps its own state and declares damage;
 * each tick coalesces the damage and issues one stark_display_render().
 * Everything here runs on the UI task, the event bus's one consumer.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stark_err.h"
#include "stark_event.h"
#include "stark_gfx.h"
#include "ui_damage.h"

typedef struct stark_screen {
    const char *name; /* shown in the status bar while this screen is on top */
    void (*on_enter)(struct stark_screen *self);                         /* optional */
    void (*on_exit)(struct stark_screen *self);                          /* optional */
    bool (*on_event)(struct stark_screen *self, const stark_event_t *e); /* true = consumed */
    void (*on_render)(struct stark_screen *self, gfx_surface_t *s);      /* required */
    void *state;
    ui_damage_t damage; /* dirty areas (ui_damage.h); empty = nothing to redraw */
} stark_screen_t;

/*
 * Subscribes the UI to every event type on the global bus (so call
 * stark_event_init() and stark_display_init() first). STARK_ERR_STATE if
 * already initialised.
 */
stark_err_t stark_ui_init(void);

/*
 * Pushes `screen` (on_render required) and calls its on_enter; the whole
 * screen, status bar included, is redrawn on the next tick. Depth is 8:
 * STARK_ERR_NO_MEM when full, STARK_ERR_INVALID_ARG for a NULL screen or
 * on_render, STARK_ERR_STATE before init.
 */
stark_err_t stark_ui_push(stark_screen_t *screen);

/*
 * Pops the top screen (calling its on_exit) and fully redraws the one
 * below. Never pops the root: STARK_ERR_STATE with one screen or none.
 */
stark_err_t stark_ui_pop(void);

/* Adds `area` (logical coordinates) to s->damage (ui_damage_add()); empty
 * areas are ignored. */
void stark_ui_invalidate(stark_screen_t *s, gfx_rect_t area);

/*
 * One UI frame: dispatch every queued event (each goes to the top screen's
 * on_event; an unconsumed BACK short press pops, and at the root is a
 * no-op), then, if the top screen or the status bar has damage, clear it
 * and render each rectangle of it once (non-overlapping, so no pixel is
 * drawn twice). Allocates nothing.
 */
void stark_ui_tick(void);

/* Frame statistics (TASKS.md STARK-0101). A frame is a tick that rendered;
 * an overrun is one whose dispatch-plus-render time exceeded
 * 1 000 000 / CONFIG_STARK_UI_TARGET_FPS µs. Informational in simulation,
 * gated only on hardware (ADR-0011). */
typedef struct {
    uint32_t frames;        /* ticks that rendered */
    uint32_t renders;       /* stark_display_render() calls */
    uint32_t overruns;      /* frames over the budget */
    uint32_t render_errors; /* failed stark_display_render() calls */
    uint32_t last_frame_us; /* duration of the latest frame */
    uint32_t max_frame_us;  /* longest frame so far */
} stark_ui_stats_t;

/*
 * Copies the counters. Callable from any task: the UI task is their only
 * writer and each one is atomic, so every field is consistent on its own
 * (a snapshot may straddle one frame). STARK_ERR_INVALID_ARG for NULL.
 */
stark_err_t stark_ui_stats(stark_ui_stats_t *out);

/*
 * The UI task body (ARCHITECTURE §7: created by main, priority 5, 6 kB,
 * core 1). Never returns: sleeps on stark_event_wait() for the rest of
 * each frame (CONFIG_STARK_UI_TARGET_FPS, 30), then ticks — with no events
 * and no damage it only wakes, it never renders.
 */
void stark_ui_task(void *arg);
