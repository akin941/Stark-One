/*
 * ui_menu.h — the list menu widget (TASKS.md STARK-0018, ARCHITECTURE.md
 * §6.7). A menu is a screen: initialise it, then stark_ui_push(&m->screen).
 * Selection arithmetic is ui_menu_model.h; this renders it and routes keys.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_ui.h"
#include "ui_menu_model.h"

/* Menu icons: 16x16, 1bpp, gfx_blit_1bpp() layout (2 bytes per row, MSB
 * leftmost) — 32 bytes. */
#define UI_MENU_ICON_SIZE 16

typedef struct {
    const char *label;
    bool disabled;       /* shown dimmed, never selected (a rendering state only) */
    bool header;         /* a section title: accent colour, never selected (STARK-0107) */
    const uint8_t *icon; /* optional UI_MENU_ICON_SIZE² 1bpp icon (NULL: none) */
} ui_menu_item_t;

/* OK (short press) on the selected item. */
typedef void (*ui_menu_activate_fn)(size_t index, void *ctx);

typedef struct {
    stark_screen_t screen; /* push this; its name is the menu's title */
    const ui_menu_item_t *items;
    size_t count;
    ui_menu_activate_fn on_activate; /* optional */
    void *ctx;
    ui_menu_model_t model; /* private */
} ui_menu_t;

/*
 * items must outlive the menu. Rows are 24 px below the status bar (nine
 * visible on 320x240) with the selection inverted and a scroll indicator at
 * the right edge while the list overflows. Header rows are drawn in the
 * accent colour and skipped like disabled ones; when any item has an icon,
 * icons sit at the text inset and every label shifts right past them. Keys: UP/DOWN (press and
 * repeat) move with wrap, LEFT/RIGHT page by one window, OK activates;
 * BACK is left to stark_ui (pop). Every selection change logs
 * `menu: sel=<n> "<label>"` at INFO and repaints only the two rows involved
 * unless the window scrolled.
 */
void ui_menu_init(ui_menu_t *m, const char *title, const ui_menu_item_t *items, size_t count,
                  ui_menu_activate_fn on_activate, void *ctx);
