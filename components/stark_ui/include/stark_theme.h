/*
 * stark_theme.h — every colour and layout constant the UI and the apps draw
 * with (TASKS.md STARK-0103; STARK-0017 started it privately as ui_theme.h).
 * Apps use these names and stark_ui_content_rect(), never literal colours,
 * the literal 320/240 or the status-bar height. A constant is added with its
 * first user; values are pinned by test/host/test_ui_apps.c.
 */
#pragma once

#include "stark_gfx.h"

/* Screen content */
#define STARK_THEME_BG           GFX_RGB565(0, 0, 0)
#define STARK_THEME_FG           GFX_RGB565(255, 255, 255)
#define STARK_THEME_ACTIVE       GFX_RGB565(255, 255, 0) /* something live: a held key, a readout */

/* Dialog (STARK-0105) */
#define STARK_THEME_ACCENT       GFX_RGB565(0, 128, 255) /* dialog border, menu headers */

/* Status bar */
#define STARK_THEME_STATUS_BG    GFX_RGB565(0, 64, 128)
#define STARK_THEME_STATUS_FG    GFX_RGB565(255, 255, 255)

/* List menu (STARK-0018): the selected row is drawn inverted */
#define STARK_THEME_SEL_BG       STARK_THEME_FG
#define STARK_THEME_SEL_FG       STARK_THEME_BG
#define STARK_THEME_DISABLED     GFX_RGB565(96, 96, 96)
#define STARK_THEME_SCROLL_BAR   GFX_RGB565(64, 64, 64)
#define STARK_THEME_SCROLL_THUMB GFX_RGB565(200, 200, 200)

/* Layout */
#define STARK_THEME_STATUSBAR_H  16 /* one gfx_font_mono16 line */
#define STARK_THEME_TITLE_X      4  /* status-bar title inset */
#define STARK_THEME_ROW_H        24 /* one list-menu item */
#define STARK_THEME_TEXT_X       8  /* label / content text inset */
#define STARK_THEME_ROW_TEXT_Y   4  /* (24 - 16) / 2: the font cell centred in its row */
#define STARK_THEME_SCROLLBAR_W  4  /* at the right edge, only when a list overflows */
#define STARK_THEME_ICON_GAP     6  /* between a menu icon and its label */
