/*
 * ui_internal.h — private to stark_ui: the status bar (ui_statusbar.c) as
 * the screen stack (ui_stack.c) uses it.
 */
#pragma once

#include <stdbool.h>
#include "stark_gfx.h"

/* The status bar's area: the full width, STARK_THEME_STATUSBAR_H rows at the top. */
gfx_rect_t ui_statusbar_rect(void);

/* The storage glyph's area at the right end of the bar (STARK-0202). */
gfx_rect_t ui_statusbar_storage_rect(void);

/* Records the glyph to draw; true when it changed. */
bool ui_statusbar_set_storage(int icon);

/* Draws the status bar: the title left-aligned, the storage glyph (if any)
 * right-aligned; battery and clock arrive with their tasks. */
void ui_statusbar_draw(gfx_surface_t *s, const char *title);
