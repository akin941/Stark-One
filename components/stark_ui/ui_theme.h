/*
 * ui_theme.h — every colour and layout constant the UI draws with, in one
 * place from the first commit (TASKS.md STARK-0017). Private to stark_ui.
 */
#pragma once

#include "stark_gfx.h"

/* Screen content */
#define UI_COLOR_BG          GFX_RGB565(0, 0, 0)
#define UI_COLOR_FG          GFX_RGB565(255, 255, 255)

/* Status bar */
#define UI_COLOR_STATUS_BG   GFX_RGB565(0, 64, 128)
#define UI_COLOR_STATUS_FG   GFX_RGB565(255, 255, 255)

/* Layout */
#define UI_STATUSBAR_H       16 /* one gfx_font_mono16 line */
#define UI_STATUSBAR_TITLE_X 4
