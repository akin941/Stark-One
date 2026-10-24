/*
 * app_storage.c — Storage (TASKS.md STARK-0202/0203): the card's state, with
 * Eject (after a confirmation), Mount and the 100 kB write test
 * (storage_wtest.c, on the app worker). Eject and Mount are requests the storage task
 * carries out; the result comes back as STARK_SYS_STORAGE and the status row
 * follows it. Every status the row shows is logged for the scenarios.
 */
#include <inttypes.h>
#include <stdio.h>
#include "stark_app.h"
#include "stark_log.h"
#include "stark_storage.h"
#include "storage_wtest.h"
#include "ui_dialog.h"
#include "ui_menu.h"

enum { ROW_STATUS, ROW_EJECT, ROW_MOUNT, ROW_WTEST };

static char s_status[48];
static char s_wtest[32] = "Write test (100 kB)";
static ui_menu_item_t s_items[] = {
    [ROW_STATUS] = {.label = s_status, .disabled = true},
    [ROW_EJECT] = {.label = "Eject"},
    [ROW_MOUNT] = {.label = "Mount"},
    [ROW_WTEST] = {.label = s_wtest},
};
static ui_menu_t s_menu;
static bool (*s_menu_event)(stark_screen_t *self, const stark_event_t *e);

/* Status text for a state; Mounted shows the space figures the storage task
 * cached at mount (never a card access from the UI task). */
static void refresh_status(void)
{
    stark_storage_state_t state = stark_storage_state();
    stark_medium_space_t space;
    switch (state) {
        case STARK_STORAGE_MOUNTED:
            if (stark_storage_space(&space) == STARK_OK) {
                snprintf(s_status, sizeof s_status, "Mounted, %" PRIu32 " of %" PRIu32 " MB free",
                         (uint32_t)(space.free_bytes >> 20), (uint32_t)(space.total_bytes >> 20));
            } else {
                snprintf(s_status, sizeof s_status, "Mounted");
            }
            break;
        case STARK_STORAGE_ABSENT:
            snprintf(s_status, sizeof s_status, "No card");
            break;
        case STARK_STORAGE_REMOVED:
            snprintf(s_status, sizeof s_status, "Card removed");
            break;
        case STARK_STORAGE_EJECTED:
            snprintf(s_status, sizeof s_status, "Ejected - safe to remove");
            break;
        case STARK_STORAGE_UNUSABLE:
            snprintf(s_status, sizeof s_status, "Card not usable");
            break;
        case STARK_STORAGE_NOT_FITTED:
            snprintf(s_status, sizeof s_status, "No card slot");
            break;
    }
    STARK_LOGI("storageapp", "status %s", stark_storage_state_str(state));
}

static void eject_confirmed(stark_dialog_result_t result, void *ctx)
{
    (void)ctx;
    if (result == STARK_DIALOG_OK) {
        STARK_LOGI("storageapp", "eject requested");
        (void)stark_storage_request_eject();
    }
}

static void activate(size_t index, void *ctx)
{
    (void)ctx;
    if (index == ROW_EJECT) {
        if (stark_storage_state() != STARK_STORAGE_MOUNTED) {
            STARK_LOGI("storageapp", "eject: not mounted");
            return;
        }
        (void)stark_ui_dialog_confirm("Eject card?", "Unmount the card so it can be removed.",
                                      eject_confirmed, NULL);
    } else if (index == ROW_MOUNT) {
        STARK_LOGI("storageapp", "mount requested");
        (void)stark_storage_request_mount();
    } else if (index == ROW_WTEST) {
        storage_wtest_start();
    }
}

static bool storage_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type == STARK_EVT_SYSTEM && e->system.id == STARK_SYS_STORAGE) {
        refresh_status();
        stark_ui_invalidate(self, stark_ui_content_rect());
        return true;
    }
    if (e->type == STARK_EVT_APP_REQUEST && e->app.id == STARK_APP_REQ_NOTIFY) {
        snprintf(s_wtest, sizeof s_wtest, "Write test  %d%%", (int)e->app.arg);
        stark_ui_invalidate(self, stark_ui_content_rect());
        return true;
    }
    return s_menu_event(self, e);
}

static stark_screen_t *storage_screen(void *state)
{
    (void)state;
    refresh_status();
    snprintf(s_wtest, sizeof s_wtest, "Write test (100 kB)");
    ui_menu_init(&s_menu, "Storage", s_items, sizeof s_items / sizeof s_items[0], activate, NULL);
    s_menu_event = s_menu.screen.on_event;
    s_menu.screen.on_event = storage_event;
    return &s_menu.screen;
}

/* Launcher icon (16x16, 1bpp — ui_menu.h): a memory card. */
static const uint8_t k_icon[32] = {
    0x0F, 0xF0, 0x1F, 0xF8, 0x35, 0x58, 0x75, 0x58, 0xF5, 0x58, 0xFF, 0xF8, 0xFF, 0xF8, 0xFF, 0xF8,
    0xFF, 0xF8, 0xFF, 0xF8, 0xFF, 0xF8, 0xFF, 0xF8, 0xFF, 0xF8, 0xFF, 0xF8, 0x00, 0x00, 0x00, 0x00,
};

const stark_app_t app_storage = {
    .id = "storage",
    .title = "Storage",
    .category = "Device",
    .screen = storage_screen,
    .icon = k_icon,
};
