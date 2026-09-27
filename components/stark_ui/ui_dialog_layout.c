/*
 * ui_dialog_layout.c — dialog geometry and UTF-8-safe copying
 * (ui_dialog_layout.h).
 */
#include "ui_dialog_layout.h"
#include <string.h>

size_t ui_utf8_copy(char *dst, size_t cap, const char *src)
{
    if (src == NULL) {
        src = "";
    }
    size_t n = strlen(src);
    if (n > cap - 1) {
        n = cap - 1;
        /* Back off over a sequence the cut went through: drop continuation
         * bytes, then the lead byte that started them. */
        if ((unsigned char)src[n] >= 0x80u && ((unsigned char)src[n] & 0xC0u) == 0x80u) {
            while (n > 0 && ((unsigned char)src[n - 1] & 0xC0u) == 0x80u) {
                n--;
            }
            if (n > 0 && (unsigned char)src[n - 1] >= 0xC0u) {
                n--;
            }
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
    return n;
}

/* Bytes of the longest code-point prefix of s that fits max_w pixels (a
 * title is one row: no word breaking). */
static size_t fitting_prefix(const gfx_font_t *f, const char *s, int16_t max_w)
{
    int32_t cols = max_w / f->w;
    size_t bytes = 0;
    for (int32_t c = 0; c < cols && s[bytes] != '\0'; c++) {
        bytes++;
        while (((unsigned char)s[bytes] & 0xC0u) == 0x80u) {
            bytes++; /* the rest of this code point */
        }
    }
    return bytes;
}

void ui_dialog_layout(const gfx_font_t *title_font, const gfx_font_t *body_font,
                      const gfx_font_t *hint_font, const char *title, const char *message,
                      gfx_rect_t content, ui_dialog_layout_t *out)
{
    if (title == NULL) {
        title = "";
    }
    if (message == NULL) {
        message = "";
    }
    const int16_t inset = UI_DIALOG_BORDER + UI_DIALOG_PAD;
    int16_t w = content.w < UI_DIALOG_W ? content.w : UI_DIALOG_W;
    int16_t inner_w = (int16_t)(w - 2 * inset);

    int16_t lines = gfx_text_lines(body_font, message, inner_w);
    if (lines > UI_DIALOG_MAX_LINES) {
        lines = UI_DIALOG_MAX_LINES;
    }
    int16_t body_h = (int16_t)(lines * body_font->h);
    int16_t h = (int16_t)(2 * inset + title_font->h + UI_DIALOG_PAD + body_h +
                          (lines > 0 ? UI_DIALOG_PAD : 0) + hint_font->h);

    int16_t x = (int16_t)(content.x + (content.w - w) / 2);
    int16_t y = (int16_t)(content.y + (content.h - h) / 2);
    out->box = (gfx_rect_t){x, y, w, h};
    out->title = (gfx_rect_t){(int16_t)(x + inset), (int16_t)(y + inset), inner_w, title_font->h};
    int16_t body_y = (int16_t)(out->title.y + title_font->h + UI_DIALOG_PAD);
    out->body = (gfx_rect_t){(int16_t)(x + inset), body_y, inner_w, body_h};
    out->hint = (gfx_rect_t){(int16_t)(x + inset),
                             (int16_t)(body_y + body_h + (lines > 0 ? UI_DIALOG_PAD : 0)), inner_w,
                             hint_font->h};
    out->lines = lines;
    out->title_bytes = fitting_prefix(title_font, title, inner_w);
}
