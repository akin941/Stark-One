/*
 * gfx_font.c — UTF-8 decoding and cell-font text (TASKS.md STARK-0014).
 */
#include "gfx_font.h"
#include <stddef.h>

#define BAD_CP UINT32_MAX /* malformed UTF-8: drawn as the fallback glyph */

/*
 * Decodes one code point at *p and advances *p past it. Malformed input —
 * a stray continuation byte, an invalid lead byte, a truncated, overlong
 * or surrogate sequence, or anything above U+10FFFF — consumes exactly one
 * byte and yields BAD_CP. A truncated sequence stops at the terminating
 * NUL (it fails the continuation test), so nothing past it is read.
 */
static uint32_t next_cp(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    uint32_t cp = s[0];
    size_t extra;
    uint32_t min;

    if (cp < 0x80u) {
        *p += 1;
        return cp;
    } else if ((cp & 0xE0u) == 0xC0u) {
        extra = 1;
        cp &= 0x1Fu;
        min = 0x80u;
    } else if ((cp & 0xF0u) == 0xE0u) {
        extra = 2;
        cp &= 0x0Fu;
        min = 0x800u;
    } else if ((cp & 0xF8u) == 0xF0u) {
        extra = 3;
        cp &= 0x07u;
        min = 0x10000u;
    } else {
        *p += 1;
        return BAD_CP;
    }

    for (size_t i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0u) != 0x80u) {
            *p += 1;
            return BAD_CP;
        }
        cp = (cp << 6) | (s[i] & 0x3Fu);
    }
    if (cp < min || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
        *p += 1;
        return BAD_CP;
    }
    *p += extra + 1;
    return cp;
}

static const uint8_t *glyph_bits(const gfx_font_t *f, uint32_t cp)
{
    if (cp < f->first || cp > f->last) {
        cp = f->fallback;
    }
    size_t glyph_bytes = (size_t)f->h * (((size_t)f->w + 7u) / 8u);
    return f->bits + (size_t)(cp - f->first) * glyph_bytes;
}

int16_t gfx_text_width(const gfx_font_t *f, const char *utf8)
{
    if (f == NULL || utf8 == NULL) {
        return 0;
    }
    int32_t width = 0;
    while (*utf8 != '\0') {
        (void)next_cp(&utf8);
        width += f->w;
        if (width >= INT16_MAX) {
            return INT16_MAX;
        }
    }
    return (int16_t)width;
}

int16_t gfx_text(gfx_surface_t *s, const gfx_font_t *f, int16_t x, int16_t y, const char *utf8,
                 uint16_t fg, uint16_t bg, bool transparent)
{
    if (f == NULL || utf8 == NULL) {
        return 0;
    }
    const char *p = utf8;
    int32_t pen = x;
    while (*p != '\0' && pen <= INT16_MAX) {
        const uint8_t *bits = glyph_bits(f, next_cp(&p));
        gfx_blit_1bpp(s, (int16_t)pen, y, bits, f->w, f->h, fg, bg, transparent);
        pen += f->w;
    }
    return gfx_text_width(f, utf8);
}
