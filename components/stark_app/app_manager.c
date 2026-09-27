/*
 * app_manager.c — app lifecycle and the launcher (TASKS.md STARK-0019,
 * ARCHITECTURE.md §6.8). Runs on the UI task, like every screen callback.
 *
 * The launcher is the root menu, built once at init from the registry in
 * category/title order. Its tables are sized by the registry and allocated
 * at init (ARCHITECTURE §8), so adding an app never touches this file.
 */
#include "stark_app.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "app_internal.h"
#include "stark_event.h"
#include "stark_log.h"
#include "ui_dialog.h"
#include "ui_menu.h"

#define LAUNCHER_TITLE "STARK ONE"

static const stark_app_t *const *s_apps; /* the registry, from stark_app_init() */
static size_t s_app_count;
static const stark_app_t **s_sorted;
static size_t s_count;
/* Launcher rows (a header per category, then its apps) and, per row, the
 * app it launches — NULL for a header. */
static ui_menu_item_t *s_items;
static const stark_app_t **s_row_app;
static size_t s_rows;
static ui_menu_t s_launcher;
static bool s_ready;

/* The running app, if any, and the on_exit its screen had before launch. */
static const stark_app_t *s_current;
static void *s_state;
static void (*s_app_on_exit)(stark_screen_t *self);

/* Fault containment (STARK-0108): every launch gets a new generation; a
 * fault report carries the one it was raised in, so a late report from an
 * app that already stopped can be told apart. Written on the UI task, read
 * from any task (stark_app_fail() may run on the worker). */
static atomic_uint_least32_t s_generation;
static atomic_int s_fault_err;
static char s_fault_msg[48]; /* the "App stopped" alert's message (copied by the dialog) */

bool app_manager_running(void)
{
    return s_current != NULL;
}

static void log_registry(const stark_app_t *const *apps, size_t n)
{
    char ids[160];
    size_t len = 0;
    ids[0] = '\0';
    for (size_t i = 0; i < n && len < sizeof ids; i++) {
        int w = snprintf(ids + len, sizeof ids - len, "%s%s", i > 0 ? "," : "", apps[i]->id);
        len += w > 0 ? (size_t)w : 0;
    }
    STARK_LOGI("app", "registry n=%u%s%s%s", (unsigned)n, n > 0 ? " " : "", ids,
               len >= sizeof ids ? "..." : "");
}

/* stark_ui pops the app screen (BACK, or stark_app_stop_current()). */
static void on_app_screen_exit(stark_screen_t *screen)
{
    void (*app_exit)(stark_screen_t *) = s_app_on_exit;
    const stark_app_t *app = s_current;
    void *state = s_state;

    screen->on_exit = app_exit; /* the screen is the app's again */
    s_current = NULL;
    s_state = NULL;
    s_app_on_exit = NULL;

    if (app_exit != NULL) {
        app_exit(screen);
    }
    if (app != NULL) {
        app_worker_join(app->id); /* the join contract: the worker is gone before on_stop */
        if (app->on_stop != NULL) {
            app->on_stop(state);
        }
        STARK_LOGI("app", "stop %s", app->id);
    }
}

void stark_app_fail(stark_err_t err)
{
    atomic_store(&s_fault_err, (int)err);
    stark_event_t e = {.type = STARK_EVT_APP_REQUEST};
    e.app.id = STARK_APP_REQ_FAIL;
    e.app.arg = (int32_t)atomic_load(&s_generation);
    (void)stark_event_publish(&e);
}

/* On the UI task: a fault report from the running app unwinds to the
 * launcher and says so; anything stale is ignored. */
static void on_app_request(const stark_event_t *e, void *ctx)
{
    (void)ctx;
    if (e->app.id != STARK_APP_REQ_FAIL) {
        return; /* NOTIFY is for the app's own screen */
    }
    uint32_t gen = (uint32_t)e->app.arg;
    if (!app_fault_accept(gen, atomic_load(&s_generation), s_current != NULL)) {
        STARK_LOGD("app", "stale fault report ignored");
        return;
    }
    const stark_app_t *app = s_current;
    stark_err_t err = (stark_err_t)atomic_load(&s_fault_err);
    STARK_LOGE("app", "fault %s: %s", app->id, stark_err_str(err));
    (void)stark_ui_pop_to_root(); /* worker joined, on_stop, `app: stop` */
    (void)snprintf(s_fault_msg, sizeof s_fault_msg, "%s: %s", app->title, stark_err_str(err));
    (void)stark_ui_dialog_alert("App stopped", s_fault_msg, NULL, NULL);
}

static void launch_fail(const char *id, stark_err_t err)
{
    STARK_LOGE("app", "start %s failed: %s", id, stark_err_str(err));
}

stark_err_t stark_app_launch(const char *id)
{
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    const stark_app_t *app = stark_app_find(id);
    if (app == NULL) {
        launch_fail(id != NULL ? id : "(null)", STARK_ERR_NOT_FOUND);
        return STARK_ERR_NOT_FOUND;
    }
    if (s_current != NULL) {
        return STARK_ERR_BUSY;
    }

    void *state = NULL;
    stark_err_t err = app->on_start != NULL ? app->on_start(&state) : STARK_OK;
    if (err != STARK_OK) {
        launch_fail(app->id, err); /* the launcher stays on screen */
        return err;
    }
    stark_screen_t *screen = app->screen != NULL ? app->screen(state) : NULL;
    if (screen == NULL) {
        err = STARK_ERR_STATE;
    } else {
        atomic_store(&s_generation, atomic_load(&s_generation) + 1u);
        s_current = app;
        s_state = state;
        s_app_on_exit = screen->on_exit;
        screen->on_exit = on_app_screen_exit;
        err = stark_ui_push(screen);
        if (err != STARK_OK) {
            screen->on_exit = s_app_on_exit;
            s_current = NULL;
            s_state = NULL;
            s_app_on_exit = NULL;
        }
    }
    if (err != STARK_OK) {
        if (app->on_stop != NULL) {
            app->on_stop(state);
        }
        launch_fail(app->id, err);
        return err;
    }
    STARK_LOGI("app", "start %s", app->id);
    return STARK_OK;
}

void stark_app_stop_current(void)
{
    if (s_current != NULL) {
        (void)stark_ui_pop();
    }
}

static void on_launcher_activate(size_t index, void *ctx)
{
    (void)ctx;
    if (index < s_rows && s_row_app[index] != NULL) {
        (void)stark_app_launch(s_row_app[index]->id); /* failures are logged inside */
    }
}

const stark_app_t *stark_app_find(const char *id)
{
    return app_catalog_find(s_apps, s_app_count, id);
}

size_t stark_app_list(const stark_app_t **out, size_t max)
{
    for (size_t i = 0; out != NULL && i < s_count && i < max; i++) {
        out[i] = s_sorted[i];
    }
    return s_count;
}

stark_err_t stark_app_init(const stark_app_t *const *apps, size_t n)
{
    if (s_ready) {
        return STARK_ERR_STATE;
    }
    if (apps == NULL && n > 0) {
        return STARK_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < n; i++) {
        if (apps[i] == NULL || apps[i]->id == NULL) {
            return STARK_ERR_INVALID_ARG;
        }
    }
    STARK_CHECK_RET(
        stark_event_subscribe(STARK_EVT_MASK(STARK_EVT_APP_REQUEST), on_app_request, NULL));
    s_apps = apps;
    s_app_count = n;
    log_registry(apps, n);

    app_catalog_row_t *rows = NULL;
    if (n > 0) {
        s_sorted = calloc(n, sizeof *s_sorted);
        s_items = calloc(2 * n, sizeof *s_items);
        s_row_app = calloc(2 * n, sizeof *s_row_app);
        rows = calloc(2 * n, sizeof *rows);
        if (s_sorted == NULL || s_items == NULL || s_row_app == NULL || rows == NULL) {
            free(s_sorted);
            free(s_items);
            free(s_row_app);
            free(rows);
            s_sorted = NULL;
            s_items = NULL;
            s_row_app = NULL;
            return STARK_ERR_NO_MEM;
        }
    }
    s_count = app_catalog_sort(apps, n, s_sorted, n);
    s_rows = app_catalog_layout(s_sorted, s_count, rows, 2 * n);
    for (size_t i = 0; i < s_rows; i++) {
        if (rows[i].app == NULL) {
            s_items[i] = (ui_menu_item_t){.label = rows[i].header, .header = true};
        } else {
            s_items[i] = (ui_menu_item_t){.label = rows[i].app->title, .icon = rows[i].app->icon};
        }
        s_row_app[i] = rows[i].app;
    }
    free(rows); /* init-time scratch (ARCHITECTURE §8: allocation at init only) */

    ui_menu_init(&s_launcher, LAUNCHER_TITLE, s_items, s_rows, on_launcher_activate, NULL);
    s_ready = true;
    stark_err_t err = stark_ui_push(&s_launcher.screen);
    if (err != STARK_OK) {
        s_ready = false;
    }
    return err;
}
