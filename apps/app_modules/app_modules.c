/*
 * app_modules.c — Modules (TASKS.md STARK-0505, ADR-0021): every entry
 * stark_module knows, and OK for one's details. Read-only: it has no write,
 * bus or SPI action. It follows STARK_SYS_MODULE, so a module removed while its
 * details are open shows as removed, never as stale data.
 */
#include "modulesapp.h"
#include "stark_app.h"
#include "stark_input.h"
#include "stark_module.h"

typedef enum { MODE_LIST, MODE_DETAIL } mode_t;

static mode_t s_mode;
static size_t s_row;
static ui_menu_t s_menu;
static bool (*s_menu_event)(stark_screen_t *self, const stark_event_t *e);
static bool modules_event(stark_screen_t *self, const stark_event_t *e);
static void show(mode_t mode);

static const char *value_of(size_t index, void *ctx)
{
    (void)ctx;
    return modulesview_value(index);
}

static void activate(size_t index, void *ctx)
{
    (void)ctx;
    if (s_mode == MODE_LIST) {
        s_row = index;
        show(MODE_DETAIL);
    }
}

/* NOTE: the app manager hooks the running screen's on_exit to stop the app
 * (app_manager.c); ui_menu_init() rewrites the whole screen, so the hook is
 * carried over (as in app_nfc.c). */
static void show(mode_t mode)
{
    void (*on_exit)(stark_screen_t *) = s_menu.screen.on_exit;
    size_t n = 0;
    const ui_menu_item_t *items =
        mode == MODE_LIST ? modulesview_list(&n) : modulesview_detail(s_row, &n);
    s_mode = mode;
    ui_menu_init(&s_menu, "Modules", items, n, activate, NULL);
    s_menu.value = value_of;
    s_menu_event = s_menu.screen.on_event;
    s_menu.screen.on_event = modules_event;
    s_menu.screen.on_exit = on_exit;
    for (size_t i = 0; mode == MODE_LIST && i < s_row; i++) {
        (void)ui_menu_model_move(&s_menu.model, 1); /* back on the row it was on */
    }
    stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
}

static bool modules_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type == STARK_EVT_SYSTEM && e->system.id == STARK_SYS_MODULE) {
        if (s_mode == MODE_LIST) {
            s_row = ui_menu_model_selected(&s_menu.model); /* the rebuilt list keeps it */
        }
        show(s_mode);
        return true;
    }
    if (s_mode == MODE_DETAIL && e->type == STARK_EVT_KEY && e->key.key == STARK_KEY_BACK &&
        e->key.action == STARK_KEY_SHORT) {
        show(MODE_LIST); /* BACK leaves the details, not the app */
        return true;
    }
    return s_menu_event(self, e);
}

static stark_screen_t *modules_screen(void *state)
{
    (void)state;
    s_row = 0;
    show(MODE_LIST);
    return &s_menu.screen;
}

/* Launcher icon (16x16, 1bpp — ui_menu.h): a module with connector pins. */
static const uint8_t k_icon[32] = {
    0x00, 0x00, 0x00, 0x00, 0x7F, 0xFE, 0x40, 0x02, 0x4C, 0x02, 0x4C, 0x02, 0x40, 0x02, 0x40, 0x02,
    0x40, 0x02, 0x7F, 0xFE, 0x12, 0x48, 0x12, 0x48, 0x12, 0x48, 0x12, 0x48, 0x00, 0x00, 0x00, 0x00,
};

const stark_app_t app_modules = {
    .id = "modules",
    .title = "Modules",
    .category = "Device",
    .screen = modules_screen,
    .icon = k_icon,
};
