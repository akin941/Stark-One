/*
 * ir_browse.c — the IR app's Browse mode (TASKS.md STARK-0306): saved
 * signals listed (each parsed, invalid files shown and never sent); OK loads
 * one for ir_send.c to confirm and transmit — own devices only. Card I/O
 * runs on the app worker; the UI task only shows the results.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir_names.h"
#include "irapp.h"
#include "stark_app.h"
#include "stark_log.h"
#include "stark_storage.h"

#define NOTE_LISTED 101
#define NOTE_LOADED 102
#define NOTE_FAILED 103

static irbrowse_entry_t s_list[IRAPP_BROWSE_MAX];
static size_t s_count;
static size_t s_pick; /* the entry being loaded / sent */
static bool s_busy;   /* a list/load worker owns irapp_work until it notifies */

static void notify(int32_t what)
{
    stark_event_t e = {.type = STARK_EVT_APP_REQUEST};
    e.app.id = STARK_APP_REQ_NOTIFY;
    e.app.arg = what;
    (void)stark_event_publish(&e);
}

static bool collect(const char *name, uint32_t size, void *ctx)
{
    (void)size;
    (void)ctx;
    size_t n = strlen(name);
    if (n < 4 || strcmp(name + n - 3, ".ir") != 0 || s_count == IRAPP_BROWSE_MAX) {
        return s_count < IRAPP_BROWSE_MAX;
    }
    snprintf(s_list[s_count++].name, sizeof s_list[0].name, "%s", name);
    return true;
}

static int browse_order(const void *a, const void *b)
{
    return ir_names_compare(((const irbrowse_entry_t *)a)->name,
                            ((const irbrowse_entry_t *)b)->name);
}

static void list_worker(void *ctx)
{
    (void)ctx;
    s_count = 0;
    if (stark_storage_list(STARK_DIR_CAPTURES, collect, NULL) == STARK_OK) {
        qsort(s_list, s_count, sizeof s_list[0], browse_order);
    }
    for (size_t i = 0; i < s_count && !stark_app_worker_should_stop(); i++) {
        irbrowse_entry_t *e = &s_list[i];
        e->valid = irapp_load(e->name, &irapp_work.signal) == STARK_OK;
        if (e->valid) {
            irapp_describe(&irapp_work.signal, e->what, sizeof e->what);
        } else {
            snprintf(e->what, sizeof e->what, "(invalid)");
        }
    }
    notify(NOTE_LISTED);
}

static void load_worker(void *ctx)
{
    (void)ctx;
    notify(irapp_load(s_list[s_pick].name, &irapp_work.signal) == STARK_OK ? NOTE_LOADED
                                                                           : NOTE_FAILED);
}

void irbrowse_open(void)
{
    s_count = 0;
    stark_storage_state_t state = stark_storage_state();
    if (state != STARK_STORAGE_MOUNTED) {
        STARK_LOGI("irapp", "browse: %s", stark_storage_state_str(state));
        irapp_show_browse(); /* an empty list says the same on screen */
        return;
    }
    s_busy = true;
    stark_err_t err = stark_app_worker_start(list_worker, NULL);
    if (err != STARK_OK) {
        s_busy = false;
        STARK_LOGW("irapp", "browse: %s", stark_err_str(err));
        irapp_show_browse();
    }
}

bool irbrowse_busy(void)
{
    return s_busy;
}

void irbrowse_stop(void)
{
    s_count = 0;
    s_busy = false;
}

void irbrowse_forget(void)
{
    s_count = 0;
}

const irbrowse_entry_t *irbrowse_entries(size_t *count)
{
    *count = s_count;
    return s_list;
}

void irbrowse_activate(size_t index)
{
    if (index >= s_count || !s_list[index].valid) {
        STARK_LOGI("irapp", "send refused: invalid file");
        return;
    }
    s_pick = index;
    s_busy = true;
    if (stark_app_worker_start(load_worker, NULL) != STARK_OK) {
        s_busy = false;
        STARK_LOGW("irapp", "send refused: busy");
    }
}

bool irbrowse_event(const stark_event_t *e)
{
    if (e->type == STARK_EVT_APP_REQUEST && e->app.id == STARK_APP_REQ_NOTIFY &&
        e->app.arg >= NOTE_LISTED && e->app.arg <= NOTE_FAILED) {
        s_busy = false;
        if (e->app.arg == NOTE_LISTED) {
            STARK_LOGI("irapp", "browse %u files", (unsigned)s_count);
            irapp_show_browse();
        } else if (e->app.arg == NOTE_LOADED) {
            irsend_confirm(s_list[s_pick].name);
        } else if (e->app.arg == NOTE_FAILED) {
            STARK_LOGW("irapp", "send refused: cannot read %s", s_list[s_pick].name);
        }
        return true;
    }
    return false;
}
