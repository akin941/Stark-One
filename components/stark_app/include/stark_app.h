/*
 * stark_app.h — L4, apps as independent modules: descriptor, explicit
 * static registry, lifecycle and the launcher (ARCHITECTURE.md §6.8,
 * ADR-0009, TASKS.md STARK-0019).
 *
 * V0 has no capability gating: the descriptor carries no capability field
 * (one arrives with the module bus at V0.5, ADR-0009).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"
#include "stark_ui.h"

typedef struct {
    const char *id;                         /* "about", "i2c_scan" — stable, used in settings */
    const char *title;                      /* menu label */
    const char *category;                   /* "System", "Lab", "Radio" — menu grouping */
    stark_err_t (*on_start)(void **state);  /* optional; a failure is logged, never fatal */
    void (*on_stop)(void *state);           /* optional */
    stark_screen_t *(*screen)(void *state); /* the app's one screen */
    const uint8_t *icon; /* optional launcher icon: 16x16 1bpp (ui_menu.h), NULL for none */
} stark_app_t;

/*
 * Takes the registry — the explicit static array the composition root
 * defines (main/app_registry.c, ADR-0009/ADR-0016); it must outlive the
 * firmware — logs it (`app: registry n=<N> <id>,<id>,…`, registry order),
 * builds the launcher — the root menu: a header row per category, the
 * categories in the order their first app appears in the registry, apps
 * by title within each, with their icons (STARK-0107) — and pushes it. Call after stark_ui_init().
 * STARK_ERR_STATE if already initialised; STARK_ERR_INVALID_ARG for a NULL array with count > 0 or
 * an entry (or its id) that is NULL.
 */
stark_err_t stark_app_init(const stark_app_t *const *apps, size_t count);

/* The registered app with this id, or NULL. */
const stark_app_t *stark_app_find(const char *id);

/* Copies up to max apps, in launcher order (headers excluded), to out;
 * returns how many exist. */
size_t stark_app_list(const stark_app_t **out, size_t max);

/*
 * on_start, then pushes the app's screen and logs `app: start <id>`. BACK on
 * that screen pops it (stark_ui), which calls on_stop and logs
 * `app: stop <id>`. A failing on_start, or a NULL screen, is logged
 * (`app: start <id> failed: <err>`) and the launcher stays — never a panic.
 * STARK_ERR_NOT_FOUND for an unknown id, STARK_ERR_BUSY while an app runs.
 * One app at a time, one screen per app.
 */
stark_err_t stark_app_launch(const char *id);

/* Pops the running app's screen (-> on_stop); nothing if none runs. */
void stark_app_stop_current(void);

/* ---- worker, join contract, fault containment (STARK-0108) ------------- */

/* app.id values of STARK_EVT_APP_REQUEST events. */
#define STARK_APP_REQ_NOTIFY 1u /* worker -> its app; arg is app-defined */
#define STARK_APP_REQ_FAIL   2u /* stark_app_fail(); arg is the launch generation */

typedef void (*stark_app_worker_fn)(void *ctx);

/*
 * Starts the running app's one background worker: a FreeRTOS task in a
 * static slot (CONFIG_STARK_APP_WORKER_STACK bytes, priority
 * CONFIG_STARK_APP_WORKER_PRIO — below the UI task — pinned to core 0), no
 * allocation. It talks to its app through the bus (STARK_APP_REQ_NOTIFY) and
 * polls stark_app_worker_should_stop(). Stopping the app — BACK, long-BACK
 * or a fault — joins it BEFORE on_stop: the manager sets the stop flag and
 * waits up to CONFIG_STARK_APP_WORKER_JOIN_MS for fn to return (`app: worker
 * <id> joined`); past that the task is deleted anyway (`app: worker <id>
 * join timeout`, counted), and on_stop still runs. UI task only.
 * STARK_ERR_STATE without a running app, STARK_ERR_BUSY while its worker
 * runs, STARK_ERR_INVALID_ARG for a NULL fn.
 */
stark_err_t stark_app_worker_start(stark_app_worker_fn fn, void *ctx);

/* For the worker: true once its app is stopping — return promptly. */
bool stark_app_worker_should_stop(void);

/* Workers deleted after ignoring the stop request, since boot (for
 * stark_diag, STARK-0109). Any task. */
uint32_t stark_app_worker_join_timeouts(void);

/*
 * Reports that the running app cannot go on (callable from the UI task or
 * its worker). The manager, on the UI task, logs `app: fault <id>: <err>`,
 * unwinds to the launcher (stark_ui_pop_to_root(): worker joined, on_stop,
 * `app: stop <id>`) and shows an "App stopped" alert — never a panic. A
 * report that arrives after that app has already stopped is ignored.
 * Containment covers reported errors, not memory corruption: a CPU
 * exception is still a reboot.
 */
void stark_app_fail(stark_err_t err);
