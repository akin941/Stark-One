/*
 * stark_app.h — L4, apps as independent modules: descriptor, explicit
 * static registry, lifecycle and the launcher (ARCHITECTURE.md §6.8,
 * ADR-0009, TASKS.md STARK-0019).
 *
 * V0 has no capability gating: the descriptor carries no capability field
 * (one arrives with the module bus at V0.5, ADR-0009).
 */
#pragma once

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
