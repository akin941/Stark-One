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

/* Draws the code points in [p, end) from pen x; end == NULL means "to the NUL". */
static void draw_run(gfx_surface_t *s, const gfx_font_t *f, int16_t x, int16_t y, const char *p,
                     const char *end, uint16_t fg, uint16_t bg, bool transparent)
{
    int32_t pen = x;
    while (*p != '\0' && (end == NULL || p < end) && pen <= INT16_MAX) {
        const uint8_t *bits = glyph_bits(f, next_cp(&p));
        gfx_blit_1bpp(s, (int16_t)pen, y, bits, f->w, f->h, fg, bg, transparent);
        pen += f->w;
    }
}

int16_t gfx_text(gfx_surface_t *s, const gfx_font_t *f, int16_t x, int16_t y, const char *utf8,
                 uint16_t fg, uint16_t bg, bool transparent)
{
    if (f == NULL || utf8 == NULL) {
        return 0;
    }
    draw_run(s, f, x, y, utf8, NULL, fg, bg, transparent);
    return gfx_text_width(f, utf8);
}

static const char *skip_spaces(const char *p)
{
    while (*p == ' ') {
        p++;
    }
    return p;
}

size_t gfx_text_line(const gfx_font_t *f, const char *utf8, int16_t max_w, const char **next)
{
    const char *dummy;
    if (next == NULL) {
        next = &dummy;
    }
    *next = utf8;
    if (f == NULL || utf8 == NULL || *utf8 == '\0' || f->w == 0) {
        return 0;
    }
    int32_t cols = max_w / f->w;
    if (cols < 1) {
        cols = 1; /* always make progress: at least one code point per line */
    }

    const char *p = utf8;
    const char *end = NULL;
    const char *last_space = NULL;
    int32_t n = 0;
    while (*p != '\0') {
        if (*p == '\n') {
            end = p;
            *next = p + 1;
            break;
        }
        const char *cp_start = p;
        uint32_t cp = next_cp(&p);
        if (n == cols) { /* this code point no longer fits */
            if (cp == ' ') {
                end = cp_start;
                *next = skip_spaces(cp_start);
            } else if (last_space != NULL) {
                end = last_space;
                *next = skip_spaces(last_space);
            } else {
                end = cp_start; /* one word wider than the line: break it */
                *next = cp_start;
            }
            break;
        }
        if (cp == ' ') {
            last_space = cp_start;
        }
        n++;
    }
    if (end == NULL) { /* the rest fits */
        end = p;
        *next = p;
    }
    while (end > utf8 && end[-1] == ' ') {
        end--; /* trailing spaces at a break are not part of the line */
    }
    return (size_t)(end - utf8);
}

int16_t gfx_text_lines(const gfx_font_t *f, const char *utf8, int16_t max_w)
{
    if (f == NULL || utf8 == NULL) {
        return 0;
    }
    int32_t lines = 0;
    const char *p = utf8;
    while (*p != '\0' && lines < INT16_MAX) {
        (void)gfx_text_line(f, p, max_w, &p);
        lines++;
    }
    return (int16_t)lines;
}

int16_t gfx_text_box(gfx_surface_t *s, const gfx_font_t *f, gfx_rect_t box, int16_t line_gap,
                     const char *utf8, uint16_t fg, uint16_t bg, bool transparent)
{
    if (s == NULL || f == NULL || utf8 == NULL || box.w <= 0 || box.h <= 0) {
        return 0;
    }
    /* Clip to the box as well, for the one case a glyph is wider than it. */
    gfx_rect_t saved = s->clip;
    int32_t x0 = saved.x > box.x ? saved.x : box.x;
    int32_t y0 = saved.y > box.y ? saved.y : box.y;
    int32_t x1 = (int32_t)saved.x + saved.w;
    int32_t y1 = (int32_t)saved.y + saved.h;
    if ((int32_t)box.x + box.w < x1) {
        x1 = (int32_t)box.x + box.w;
    }
    if ((int32_t)box.y + box.h < y1) {
        y1 = (int32_t)box.y + box.h;
    }
    s->clip = x1 > x0 && y1 > y0
                  ? (gfx_rect_t){(int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)}
                  : (gfx_rect_t){0, 0, 0, 0};

    int32_t pitch = (int32_t)f->h + line_gap;
    int32_t y = box.y;
    int16_t drawn = 0;
    const char *p = utf8;
    while (*p != '\0' && y + f->h <= (int32_t)box.y + box.h && drawn < INT16_MAX) {
        const char *next;
        size_t len = gfx_text_line(f, p, box.w, &next);
        draw_run(s, f, box.x, (int16_t)y, p, p + len, fg, bg, transparent);
        drawn++;
        y += pitch > 0 ? pitch : 1;
        p = next;
    }
    s->clip = saved;
    return drawn;
}
