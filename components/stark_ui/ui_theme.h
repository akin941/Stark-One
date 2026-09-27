/*
 * ui_theme.h — every colour and layout constant the UI draws with, in one
 * place from the first commit (TASKS.md STARK-0017). Private to stark_ui.
 */
#pragma once

#include "stark_gfx.h"

/* Screen content */
#define UI_COLOR_BG           GFX_RGB565(0, 0, 0)
#define UI_COLOR_FG           GFX_RGB565(255, 255, 255)

/* Status bar */
#define UI_COLOR_STATUS_BG    GFX_RGB565(0, 64, 128)
#define UI_COLOR_STATUS_FG    GFX_RGB565(255, 255, 255)

/* List menu (STARK-0018): the selected row is drawn inverted */
#define UI_COLOR_SEL_BG       UI_COLOR_FG
#define UI_COLOR_SEL_FG       UI_COLOR_BG
#define UI_COLOR_DISABLED     GFX_RGB565(96, 96, 96)
#define UI_COLOR_SCROLL_BAR   GFX_RGB565(64, 64, 64)
#define UI_COLOR_SCROLL_THUMB GFX_RGB565(200, 200, 200)

/* Layout */
#define UI_STATUSBAR_H        16 /* one gfx_font_mono16 line */
#define UI_STATUSBAR_TITLE_X  4
#define UI_MENU_ROW_H         24 /* one item */
#define UI_MENU_TEXT_X        8  /* label inset */
#define UI_MENU_TEXT_Y        4  /* (24 - 16) / 2: the font cell centred in its row */
#define UI_SCROLLBAR_W        4  /* at the right edge, only when the list overflows */
