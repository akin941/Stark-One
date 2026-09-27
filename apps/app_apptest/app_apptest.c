/*
 * app_apptest.c — App Test (TASKS.md STARK-0105): a list of platform
 * services to exercise. `Confirm dialog` asks, and on OK opens a `Done`
 * alert from the confirm's `done` (a dialog opened from a dialog's result);
 * `Alert dialog` shows an alert. Every outcome is logged for the scenarios.
 */
#include <stdio.h>
#include "stark_app.h"
#include "stark_log.h"
#include "ui_dialog.h"
#include "ui_menu.h"

static const ui_menu_item_t k_items[] = {
    {"Confirm dialog", false},
    {"Alert dialog", false},
};

static ui_menu_t s_menu;

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
    } else {
        (void)stark_ui_dialog_alert("Alert", "This is an alert. OK or BACK closes it.",
                                    alert_closed, "alert closed");
    }
}

static stark_screen_t *apptest_screen(void *state)
{
    (void)state;
    ui_menu_init(&s_menu, "App Test", k_items, sizeof k_items / sizeof k_items[0], activate, NULL);
    return &s_menu.screen;
}

const stark_app_t app_apptest = {
    .id = "apptest",
    .title = "App Test",
    .category = "Tests",
    .screen = apptest_screen,
};
