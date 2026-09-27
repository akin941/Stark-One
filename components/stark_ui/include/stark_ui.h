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
#include "stark_err.h"
#include "stark_event.h"
#include "stark_gfx.h"

typedef struct stark_screen {
    const char *name; /* shown in the status bar while this screen is on top */
    void (*on_enter)(struct stark_screen *self);                         /* optional */
    void (*on_exit)(struct stark_screen *self);                          /* optional */
    bool (*on_event)(struct stark_screen *self, const stark_event_t *e); /* true = consumed */
    void (*on_render)(struct stark_screen *self, gfx_surface_t *s);      /* required */
    void *state;
    gfx_rect_t damage; /* union of dirty areas; empty = nothing to redraw */
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

/* Adds `area` (logical coordinates) to s->damage; empty areas are ignored. */
void stark_ui_invalidate(stark_screen_t *s, gfx_rect_t area);

/*
 * One UI frame: dispatch every queued event (each goes to the top screen's
 * on_event; an unconsumed BACK short press pops, and at the root is a
 * no-op), then, if the top screen or the status bar has damage, render
 * the union of it once and clear it. Allocates nothing.
 */
void stark_ui_tick(void);

/*
 * The UI task body (ARCHITECTURE §7: created by main, priority 5, 6 kB,
 * core 1). Never returns: sleeps on stark_event_wait() for the rest of
 * each frame (CONFIG_STARK_UI_TARGET_FPS, 30), then ticks — with no events
 * and no damage it only wakes, it never renders.
 */
void stark_ui_task(void *arg);
