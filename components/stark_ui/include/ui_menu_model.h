/*
 * ui_menu_model.h — pure list-menu arithmetic: selection, wrap, paging and
 * the scroll window (TASKS.md STARK-0018). No drawing, no ESP-IDF: the
 * widget in ui_menu.c renders it; host tests cover it.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Whether item `index` can be selected; NULL means every item can. */
typedef bool (*ui_menu_enabled_fn)(size_t index, void *ctx);

/* Treat as private; read through the accessors below. */
typedef struct {
    size_t count;   /* items in the list */
    size_t visible; /* rows the window shows (>= 1) */
    size_t sel;     /* selected index; meaningful only if has_sel */
    size_t top;     /* index shown in the window's first row */
    bool has_sel;   /* false for an empty list or one with no enabled item */
    ui_menu_enabled_fn enabled;
    void *ctx;
} ui_menu_model_t;

/*
 * Selects the first enabled item (none: no selection) with the window at
 * the top. visible == 0 is treated as 1.
 */
void ui_menu_model_init(ui_menu_model_t *m, size_t count, size_t visible,
                        ui_menu_enabled_fn enabled, void *ctx);

/*
 * UP (delta < 0) / DOWN (delta > 0) by one enabled item, wrapping past
 * either end and skipping disabled items. Returns true if the selection
 * changed (false for a list with at most one enabled item).
 */
bool ui_menu_model_move(ui_menu_model_t *m, int delta);

/*
 * LEFT (dir < 0) / RIGHT (dir > 0) by one window: the selection jumps
 * `visible` items, clamped to the first/last item (paging never wraps),
 * then onto the nearest enabled item in the paging direction, or back
 * towards the start if there is none that way. Returns true if it changed.
 */
bool ui_menu_model_page(ui_menu_model_t *m, int dir);

bool ui_menu_model_has_selection(const ui_menu_model_t *m);
size_t ui_menu_model_selected(const ui_menu_model_t *m);
/* First visible index; the window always contains the selection and never
 * shows empty rows past the end while the list is longer than it. With a
 * window of two rows or more, a selection on the first row never hides a
 * non-selectable row (a header) directly above it. */
size_t ui_menu_model_top(const ui_menu_model_t *m);
