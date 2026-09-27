/*
 * ui_dialog_layout.h — private to stark_ui: the pure geometry and string
 * handling of the modal dialog (TASKS.md STARK-0105). No drawing, no
 * ESP-IDF; host-tested (test/host/test_dialog_layout.c).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "gfx_font.h"

#define UI_DIALOG_W         280 /* box width, centred in the content area */
#define UI_DIALOG_BORDER    2
#define UI_DIALOG_PAD       6
#define UI_DIALOG_MAX_LINES 4   /* message lines shown; the rest are clipped */
#define UI_DIALOG_TITLE_MAX 32  /* bytes incl. NUL */
#define UI_DIALOG_MSG_MAX   160 /* bytes incl. NUL */

typedef struct {
    gfx_rect_t box;     /* the whole dialog, border included */
    gfx_rect_t title;   /* title row (title font) */
    gfx_rect_t body;    /* message lines (body font); h = 0 without a message */
    gfx_rect_t hint;    /* key hint row (hint font) */
    int16_t lines;      /* message lines drawn: 0 .. UI_DIALOG_MAX_LINES */
    size_t title_bytes; /* title prefix that fits title.w, on a code point */
} ui_dialog_layout_t;

/*
 * Lays out a dialog for `title` and `message` (either may be ""; NULL is
 * treated as "") in `content`: UI_DIALOG_W wide (narrowed to content if it
 * is smaller), height from the wrapped message, centred.
 */
void ui_dialog_layout(const gfx_font_t *title_font, const gfx_font_t *body_font,
                      const gfx_font_t *hint_font, const char *title, const char *message,
                      gfx_rect_t content, ui_dialog_layout_t *out);

/*
 * Copies src into dst (cap bytes, cap >= 1), truncating on a UTF-8 code
 * point boundary: a sequence cut by the cap is dropped whole. NULL src
 * copies "". Returns the bytes copied (excluding the NUL).
 */
size_t ui_utf8_copy(char *dst, size_t cap, const char *src);
