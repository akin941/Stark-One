/*
 * ui_statusbar.c — the 16 px status bar (TASKS.md STARK-0017): the title,
 * and the storage glyph at the right end (STARK-0202).
 */
#include <stdbool.h>
#include <stddef.h>
#include "gfx_font.h"
#include "stark_display.h"
#include "stark_theme.h"
#include "stark_ui.h"
#include "ui_internal.h"

#define CARD_W 8
#define CARD_H 10

/* A microSD card, its top-left corner cut: filled and outline (1bpp, MSB
 * leftmost, one byte per row). */
static const uint8_t k_card_filled[CARD_H] = {0x3F, 0x7F, 0xFF, 0xFF, 0xFF,
                                              0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint8_t k_card_outline[CARD_H] = {0x3F, 0x41, 0x81, 0x81, 0x81,
                                               0x81, 0x81, 0x81, 0x81, 0xFF};

static stark_ui_storage_icon_t s_storage = STARK_UI_STORAGE_NONE;

gfx_rect_t ui_statusbar_rect(void)
{
    return (gfx_rect_t){0, 0, stark_display_width(), STARK_THEME_STATUSBAR_H};
}

gfx_rect_t ui_statusbar_storage_rect(void)
{
    return (gfx_rect_t){(int16_t)(stark_display_width() - STARK_THEME_TITLE_X - CARD_W),
                        (STARK_THEME_STATUSBAR_H - CARD_H) / 2, CARD_W, CARD_H};
}

bool ui_statusbar_set_storage(int icon)
{
    bool changed = (int)s_storage != icon;
    s_storage = (stark_ui_storage_icon_t)icon;
    return changed;
}

static void draw_storage(gfx_surface_t *s)
{
    const uint8_t *bits = k_card_outline;
    uint16_t colour = STARK_THEME_STATUS_FG;
    switch (s_storage) {
        case STARK_UI_STORAGE_NONE:
            return;
        case STARK_UI_STORAGE_ABSENT:
            colour = STARK_THEME_STATUS_DIM;
            break;
        case STARK_UI_STORAGE_MOUNTED:
            bits = k_card_filled;
            break;
        case STARK_UI_STORAGE_ERROR:
            bits = k_card_filled;
            colour = STARK_THEME_ERROR;
            break;
        case STARK_UI_STORAGE_EJECTED:
            break;
    }
    gfx_rect_t r = ui_statusbar_storage_rect();
    gfx_blit_1bpp(s, r.x, r.y, bits, CARD_W, CARD_H, colour, STARK_THEME_STATUS_BG, true);
}

void ui_statusbar_draw(gfx_surface_t *s, const char *title)
{
    gfx_fill(s, ui_statusbar_rect(), STARK_THEME_STATUS_BG);
    if (title != NULL) {
        (void)gfx_text(s, &gfx_font_mono16, STARK_THEME_TITLE_X, 0, title, STARK_THEME_STATUS_FG,
                       STARK_THEME_STATUS_BG, true);
    }
    draw_storage(s);
}
