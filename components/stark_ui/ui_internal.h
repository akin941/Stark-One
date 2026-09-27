/*
 * ui_internal.h — private to stark_ui: the status bar (ui_statusbar.c) as
 * the screen stack (ui_stack.c) uses it.
 */
#pragma once

#include "stark_gfx.h"

/* The status bar's area: the full width, STARK_THEME_STATUSBAR_H rows at the top. */
gfx_rect_t ui_statusbar_rect(void);

/* Draws the status bar: the title left-aligned; the right side is
 * deliberately empty (battery/SD/clock arrive with their tasks). */
void ui_statusbar_draw(gfx_surface_t *s, const char *title);
