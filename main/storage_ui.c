/*
 * storage_ui.c — storage state → status bar and alert (storage_ui.h).
 * Runs on the UI task: it is the event bus's one consumer.
 */
#include "storage_ui.h"
#include "stark_event.h"
#include "stark_storage.h"
#include "stark_ui.h"
#include "ui_dialog.h"

static stark_storage_state_t s_last = STARK_STORAGE_NOT_FITTED;

static stark_ui_storage_icon_t icon_for(stark_storage_state_t state)
{
    switch (state) {
        case STARK_STORAGE_NOT_FITTED:
            return STARK_UI_STORAGE_NONE;
        case STARK_STORAGE_ABSENT:
            return STARK_UI_STORAGE_ABSENT;
        case STARK_STORAGE_MOUNTED:
            return STARK_UI_STORAGE_MOUNTED;
        case STARK_STORAGE_UNUSABLE:
        case STARK_STORAGE_REMOVED:
            return STARK_UI_STORAGE_ERROR;
        case STARK_STORAGE_EJECTED:
            return STARK_UI_STORAGE_EJECTED;
    }
    return STARK_UI_STORAGE_NONE;
}

static void on_storage(const stark_event_t *e, void *ctx)
{
    (void)ctx;
    if (e->system.id != STARK_SYS_STORAGE) {
        return;
    }
    stark_storage_state_t state = (stark_storage_state_t)e->system.arg;
    stark_ui_status_set_storage(icon_for(state));
    /* Only an unexpected loss is announced; an eject was the user's own act.
     * If another dialog is open the glyph alone tells (the alert is BUSY). */
    if (s_last == STARK_STORAGE_MOUNTED && state == STARK_STORAGE_REMOVED) {
        (void)stark_ui_dialog_alert("SD card removed", "Data being written may be incomplete.",
                                    NULL, NULL);
    }
    s_last = state;
}

stark_err_t storage_ui_init(void)
{
    return stark_event_subscribe(STARK_EVT_MASK(STARK_EVT_SYSTEM), on_storage, NULL);
}
