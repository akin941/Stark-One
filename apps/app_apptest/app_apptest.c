/*
 * app_apptest.c — App Test (TASKS.md STARK-0105, STARK-0108): a list of
 * platform services to exercise. `Confirm dialog` asks, and on OK opens a
 * `Done` alert from the confirm's `done` (a dialog opened from a dialog's
 * result); `Alert dialog` shows an alert; the worker and fault items are in
 * app_apptest_worker.c. Every outcome is logged for the scenarios.
 */
#include <stdio.h>
#include "apptest.h"
#include "stark_app.h"
#include "stark_log.h"
#include "ui_dialog.h"
#include "ui_menu.h"

static char s_worker_label[24] = "Worker demo";

static const ui_menu_item_t k_items[] = {
    {.label = "Confirm dialog"}, {.label = "Alert dialog"},     {.label = s_worker_label},
    {.label = "Fail"},           {.label = "Fail from worker"}, {.label = "Worker ignores stop"},
};

static ui_menu_t s_menu;
static bool (*s_menu_event)(stark_screen_t *self, const stark_event_t *e);

static void alert_closed(stark_dialog_result_t result, void *ctx)
{
    (void)result;
    STARK_LOGI("apptest", "%s", (const char *)ctx);
}

static void confirm_done(stark_dialog_result_t result, void *ctx)
{
    (void)ctx;
    if (result == STARK_DIALOG_OK) {
        STARK_LOGI("apptest", "confirmed");
        (void)stark_ui_dialog_alert("Done", "Confirmed.", alert_closed, "done closed");
    } else {
        STARK_LOGI("apptest", "cancelled");
    }
}

static void activate(size_t index, void *ctx)
{
    (void)ctx;
    if (index == 0) {
        /* A stack buffer on purpose: the dialog copies what it shows. */
        char message[64];
        snprintf(message, sizeof message, "Run the confirmed action? (%s)", "App Test");
        (void)stark_ui_dialog_confirm("Confirm", message, confirm_done, NULL);
    } else if (index == 1) {
        (void)stark_ui_dialog_alert("Alert", "This is an alert. OK or BACK closes it.",
                                    alert_closed, "alert closed");
    } else {
        apptest_run(index - 2); /* the worker and fault items */
    }
}

/* The worker's progress arrives as STARK_APP_REQ_NOTIFY; keys go to the menu. */
static bool apptest_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type == STARK_EVT_APP_REQUEST && e->app.id == STARK_APP_REQ_NOTIFY) {
        snprintf(s_worker_label, sizeof s_worker_label, "Worker demo  n=%d", (int)e->app.arg);
        STARK_LOGI("apptest", "worker n=%d", (int)e->app.arg);
        stark_ui_invalidate(self, stark_ui_content_rect());
        return true;
    }
    return s_menu_event(self, e);
}

static stark_screen_t *apptest_screen(void *state)
{
    (void)state;
    snprintf(s_worker_label, sizeof s_worker_label, "Worker demo");
    ui_menu_init(&s_menu, "App Test", k_items, sizeof k_items / sizeof k_items[0], activate, NULL);
    s_menu_event = s_menu.screen.on_event;
    s_menu.screen.on_event = apptest_event;
    return &s_menu.screen;
}

/* Launcher icon (16x16, 1bpp — ui_menu.h). */
static const uint8_t k_icon[32] = {
    0x00, 0x00, 0xFF, 0xFC, 0x80, 0x04, 0x80, 0x14, 0x80, 0x34, 0x80, 0x64, 0xA0, 0xC4, 0xB1, 0x84,
    0x9B, 0x04, 0x8E, 0x04, 0x84, 0x04, 0x80, 0x04, 0xFF, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

const stark_app_t app_apptest = {
    .id = "apptest",
    .title = "App Test",
    .category = "Tests",
    .screen = apptest_screen,
    .icon = k_icon,
};
