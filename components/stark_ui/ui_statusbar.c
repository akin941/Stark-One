/*
 * ui_statusbar.c — the 16 px status bar (TASKS.md STARK-0017).
 */
#include "gfx_font.h"
#include "stark_display.h"
#include "ui_internal.h"
#include "ui_theme.h"

gfx_rect_t ui_statusbar_rect(void)
{
    return (gfx_rect_t){0, 0, stark_display_width(), UI_STATUSBAR_H};
}

void ui_statusbar_draw(gfx_surface_t *s, const char *title)
{
    gfx_fill(s, ui_statusbar_rect(), UI_COLOR_STATUS_BG);
    if (title != NULL) {
        (void)gfx_text(s, &gfx_font_mono16, UI_STATUSBAR_TITLE_X, 0, title, UI_COLOR_STATUS_FG,
                       UI_COLOR_STATUS_BG, true);
    }
}
