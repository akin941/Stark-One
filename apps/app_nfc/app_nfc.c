/*
 * app_nfc.c — NFC (TASKS.md STARK-0404, ADR-0020): while the app is open the
 * NFC task polls (the RF field is on only then). One menu shows either the
 * tag view (nfc_view.c) or the write payloads (nfc_write.c); BACK on the
 * payloads returns to the view. Without a module the app says so and starts
 * nothing.
 */
#include "nfcapp.h"
#include "stark_app.h"
#include "stark_input.h"
#include "stark_log.h"

typedef enum { MODE_VIEW, MODE_WRITE } mode_t;

static mode_t s_mode;
static ui_menu_t s_menu;
static bool (*s_menu_event)(stark_screen_t *self, const stark_event_t *e);

static const char *value_of(size_t index, void *ctx)
{
    (void)ctx;
    return s_mode == MODE_VIEW ? nfcview_value(index) : NULL;
}

static void activate(size_t index, void *ctx)
{
    (void)ctx;
    if (s_mode == MODE_WRITE) {
        nfcwrite_activate(index);
    } else if (nfcview_is_write_row(index)) {
        nfcapp_show_write();
    } else if (nfcview_is_save_row(index)) {
        nfcsave_request();
    }
}

/* Re-initialises the one menu for a mode (selection starts at the top).
 * NOTE: the app manager hooks the running screen's on_exit to stop the app
 * (app_manager.c); ui_menu_init() rewrites the whole screen, so the hook is
 * carried over (as in app_ir.c). */
static bool nfc_event(stark_screen_t *self, const stark_event_t *e);

static void show(mode_t mode)
{
    void (*on_exit)(stark_screen_t *) = s_menu.screen.on_exit;
    size_t n = 0;
    const ui_menu_item_t *items = mode == MODE_VIEW ? nfcview_items(&n) : nfcwrite_items(&n);
    s_mode = mode;
    /* one title for both modes: the status bar does not redraw on a re-init */
    ui_menu_init(&s_menu, "NFC", items, n, activate, NULL);
    s_menu.value = value_of;
    s_menu_event = s_menu.screen.on_event;
    s_menu.screen.on_event = nfc_event;
    s_menu.screen.on_exit = on_exit;
    stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
}

void nfcapp_show_write(void)
{
    show(MODE_WRITE);
}

void nfcapp_show_view(void)
{
    show(MODE_VIEW);
}

static bool nfc_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type == STARK_EVT_SYSTEM && e->system.id == STARK_SYS_NFC) {
        nfcwrite_event(e);
        nfcsave_event(e);
        nfcview_refresh();
        if (e->system.arg == STARK_NFC_EVT_TAG) {
            nfcview_log_records();
        }
        bool write_over = e->system.arg == STARK_NFC_EVT_WRITE_DONE ||
                          e->system.arg == STARK_NFC_EVT_WRITE_FAILED;
        if (s_mode == MODE_VIEW || write_over) {
            show(MODE_VIEW);
        }
        return true;
    }
    if (s_mode == MODE_WRITE && e->type == STARK_EVT_KEY && e->key.key == STARK_KEY_BACK &&
        e->key.action == STARK_KEY_SHORT) {
        show(MODE_VIEW); /* BACK leaves the payloads, not the app */
        return true;
    }
    return s_menu_event(self, e);
}

static stark_err_t nfc_start(void **state)
{
    (void)state;
    nfcview_refresh();
    if (stark_nfc_start() != STARK_OK) {
        STARK_LOGI("nfcapp", "no module"); /* the app shows it and polls nothing */
    }
    return STARK_OK;
}

static void nfc_stop(void *state)
{
    (void)state;
    (void)stark_nfc_stop();
    nfcsave_stop();
}

static stark_screen_t *nfc_screen(void *state)
{
    (void)state;
    show(MODE_VIEW);
    return &s_menu.screen;
}

/* Launcher icon (16x16, 1bpp — ui_menu.h): a card with field arcs. */
static const uint8_t k_icon[32] = {
    0x00, 0x00, 0x00, 0x24, 0x00, 0x12, 0x3F, 0x89, 0x20, 0x85, 0x20, 0x85, 0x2E, 0x85, 0x2E, 0x85,
    0x20, 0x85, 0x20, 0x85, 0x3F, 0x89, 0x00, 0x12, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

const stark_app_t app_nfc = {
    .id = "nfc",
    .title = "NFC",
    .category = "Lab",
    .on_start = nfc_start,
    .on_stop = nfc_stop,
    .screen = nfc_screen,
    .icon = k_icon,
};
