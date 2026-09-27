/*
 * app_manager.c — app lifecycle and the launcher (TASKS.md STARK-0019,
 * ARCHITECTURE.md §6.8). Runs on the UI task, like every screen callback.
 *
 * The launcher is the root menu, built once at init from the registry in
 * category/title order. Its tables are sized by the registry and allocated
 * at init (ARCHITECTURE §8), so adding an app never touches this file.
 */
#include "stark_app.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "app_internal.h"
#include "stark_log.h"
#include "ui_menu.h"

#define LAUNCHER_TITLE "STARK ONE"

static const stark_app_t **s_sorted;
static ui_menu_item_t *s_items;
static size_t s_count;
static ui_menu_t s_launcher;
static bool s_ready;

/* The running app, if any, and the on_exit its screen had before launch. */
static const stark_app_t *s_current;
static void *s_state;
static void (*s_app_on_exit)(stark_screen_t *self);

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
        if (app->on_stop != NULL) {
            app->on_stop(state);
        }
        STARK_LOGI("app", "stop %s", app->id);
    }
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
    (void)stark_app_launch(s_sorted[index]->id); /* failures are logged inside */
}

const stark_app_t *stark_app_find(const char *id)
{
    size_t n;
    const stark_app_t *const *apps = app_registry(&n);
    return app_catalog_find(apps, n, id);
}

size_t stark_app_list(const stark_app_t **out, size_t max)
{
    for (size_t i = 0; out != NULL && i < s_count && i < max; i++) {
        out[i] = s_sorted[i];
    }
    return s_count;
}

stark_err_t stark_app_init(void)
{
    if (s_ready) {
        return STARK_ERR_STATE;
    }
    size_t n;
    const stark_app_t *const *apps = app_registry(&n);
    log_registry(apps, n);

    if (n > 0) {
        s_sorted = calloc(n, sizeof *s_sorted);
        s_items = calloc(n, sizeof *s_items);
        if (s_sorted == NULL || s_items == NULL) {
            free(s_sorted);
            free(s_items);
            s_sorted = NULL;
            s_items = NULL;
            return STARK_ERR_NO_MEM;
        }
    }
    s_count = app_catalog_sort(apps, n, s_sorted, n);
    for (size_t i = 0; i < s_count; i++) {
        s_items[i] = (ui_menu_item_t){.label = s_sorted[i]->title};
    }

    ui_menu_init(&s_launcher, LAUNCHER_TITLE, s_items, s_count, on_launcher_activate, NULL);
    s_ready = true;
    stark_err_t err = stark_ui_push(&s_launcher.screen);
    if (err != STARK_OK) {
        s_ready = false;
    }
    return err;
}
