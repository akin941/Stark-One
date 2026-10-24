/*
 * app_ir.c — IR (TASKS.md STARK-0305/0306): receive-first. While the app is
 * open the receiver listens. One menu shows either the receive view
 * (ir_view.c) or the saved files (ir_browse.c, sent through ir_send.c); BACK
 * on the files returns to the view. Leaving the app turns the receiver off.
 */
#include "irapp.h"
#include "stark_app.h"
#include "stark_input.h"
#include "stark_ir.h"
#include "stark_log.h"
#include "stark_storage.h"
#include "ui_menu.h"

typedef enum { MODE_VIEW, MODE_BROWSE } mode_t;

static ui_menu_item_t s_files[IRAPP_BROWSE_MAX];
static mode_t s_mode;
static ui_menu_t s_menu;
static bool (*s_menu_event)(stark_screen_t *self, const stark_event_t *e);

static const char *value_of(size_t index, void *ctx)
{
    (void)ctx;
    if (s_mode == MODE_BROWSE) {
        size_t n = 0;
        const irbrowse_entry_t *e = irbrowse_entries(&n);
        return index < n ? e[index].what : NULL;
    }
    return irview_value(index);
}

static void show(mode_t mode);

static void activate(size_t index, void *ctx)
{
    (void)ctx;
    if (s_mode == MODE_BROWSE) {
        irbrowse_activate(index);
    } else {
        irview_activate(index);
    }
}

static bool ir_event(stark_screen_t *self, const stark_event_t *e)
{
    irsend_event(e); /* logs a transmission's result; the view still sees it */
    if (irbrowse_event(e)) {
        return true;
    }
    if (s_mode == MODE_BROWSE && e->type == STARK_EVT_SYSTEM && e->system.id == STARK_SYS_STORAGE &&
        e->system.arg != (int32_t)STARK_STORAGE_MOUNTED) {
        STARK_LOGI("irapp", "browse: card gone");
        irbrowse_forget();
        show(MODE_BROWSE);
        return false; /* the status bar still wants it */
    }
    if (s_mode == MODE_BROWSE && e->type == STARK_EVT_KEY && e->key.key == STARK_KEY_BACK &&
        e->key.action == STARK_KEY_SHORT) {
        show(MODE_VIEW); /* BACK leaves the list, not the app */
        return true;
    }
    if (e->type == STARK_EVT_SYSTEM && e->system.id == STARK_SYS_IR &&
        e->system.arg == STARK_IR_EVT_RX) {
        irview_drain();
        if (s_mode == MODE_VIEW) {
            size_t n = 0;
            (void)irview_items(&n); /* rows rebuilt in place: the selection stays */
            stark_ui_invalidate(self, stark_ui_content_rect());
        }
        return true;
    }
    return s_menu_event(self, e);
}

static stark_err_t ir_start(void **state)
{
    (void)state;
    irview_reset();
    return stark_ir_rx_start(); /* receive-first: listening while the app is open */
}

static void ir_stop(void *state)
{
    (void)state;
    (void)stark_ir_rx_stop();
    irbrowse_stop(); /* the worker is joined before on_stop: nothing is in flight */
    irsend_stop();
}

/* Re-initialises the one menu for a mode (selection starts at the top).
 * NOTE: the app manager hooks the running screen's on_exit to stop the app
 * (app_manager.c); ui_menu_init() rewrites the whole screen, so a mode switch
 * must carry that hook over or BACK would pop the screen without on_stop. */
static void show(mode_t mode)
{
    void (*on_exit)(stark_screen_t *) = s_menu.screen.on_exit;
    s_mode = mode;
    if (mode == MODE_VIEW) {
        size_t n = 0;
        const ui_menu_item_t *items = irview_items(&n);
        ui_menu_init(&s_menu, "IR", items, n, activate, NULL);
    } else {
        size_t n = 0;
        const irbrowse_entry_t *e = irbrowse_entries(&n);
        for (size_t i = 0; i < n; i++) {
            s_files[i] = (ui_menu_item_t){.label = e[i].name, .disabled = !e[i].valid};
        }
        ui_menu_init(&s_menu, "IR files", s_files, n, activate, NULL);
    }
    s_menu.value = value_of;
    s_menu_event = s_menu.screen.on_event;
    s_menu.screen.on_event = ir_event;
    s_menu.screen.on_exit = on_exit;
    stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
}

void irapp_show_browse(void)
{
    show(MODE_BROWSE);
}

static stark_screen_t *ir_screen(void *state)
{
    (void)state;
    show(MODE_VIEW);
    return &s_menu.screen;
}

/* Launcher icon (16x16, 1bpp — ui_menu.h): an LED with rays. */
static const uint8_t k_icon[32] = {
    0x00, 0x00, 0x24, 0x48, 0x12, 0x90, 0x09, 0x20, 0x00, 0x00, 0x07, 0xC0, 0x0F, 0xE0, 0x0F, 0xE0,
    0x0F, 0xE0, 0x0F, 0xE0, 0x1F, 0xF0, 0x04, 0x40, 0x04, 0x40, 0x04, 0x40, 0x04, 0x40, 0x00, 0x00,
};

const stark_app_t app_ir = {
    .id = "ir",
    .title = "IR",
    .category = "Lab",
    .on_start = ir_start,
    .on_stop = ir_stop,
    .screen = ir_screen,
    .icon = k_icon,
};
