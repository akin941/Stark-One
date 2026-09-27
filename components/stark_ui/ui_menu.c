/*
 * ui_menu.c — list menu rendering, key handling and damage (TASKS.md
 * STARK-0018). The arithmetic lives in ui_menu_model.c.
 */
#include "ui_menu.h"
#include "gfx_font.h"
#include "stark_display.h"
#include "stark_input.h"
#include "stark_log.h"
#include "ui_theme.h"

static bool item_enabled(size_t index, void *ctx)
{
    const ui_menu_t *m = ctx;
    return !m->items[index].disabled;
}

static size_t visible_rows(void)
{
    return (size_t)((stark_display_height() - UI_STATUSBAR_H) / UI_MENU_ROW_H);
}

static bool overflows(const ui_menu_t *m)
{
    return m->count > visible_rows();
}

static gfx_rect_t list_rect(void)
{
    return (gfx_rect_t){0, UI_STATUSBAR_H, stark_display_width(),
                        (int16_t)(stark_display_height() - UI_STATUSBAR_H)};
}

/* The screen row an item occupies (only valid while it is in the window). */
static gfx_rect_t row_rect(const ui_menu_t *m, size_t index)
{
    size_t row = index - ui_menu_model_top(&m->model);
    int16_t w = (int16_t)(stark_display_width() - (overflows(m) ? UI_SCROLLBAR_W : 0));
    return (gfx_rect_t){0, (int16_t)(UI_STATUSBAR_H + (int)row * UI_MENU_ROW_H), w, UI_MENU_ROW_H};
}

static void draw_scrollbar(const ui_menu_t *m, gfx_surface_t *s)
{
    gfx_rect_t list = list_rect();
    gfx_rect_t track = {(int16_t)(list.w - UI_SCROLLBAR_W), list.y, UI_SCROLLBAR_W, list.h};
    gfx_fill(s, track, UI_COLOR_SCROLL_BAR);
    size_t rows = visible_rows();
    int32_t thumb_h = (int32_t)list.h * (int32_t)rows / (int32_t)m->count;
    int32_t thumb_y = (int32_t)list.h * (int32_t)ui_menu_model_top(&m->model) / (int32_t)m->count;
    gfx_fill(s,
             (gfx_rect_t){track.x, (int16_t)(list.y + thumb_y), UI_SCROLLBAR_W,
                          (int16_t)(thumb_h < 4 ? 4 : thumb_h)},
             UI_COLOR_SCROLL_THUMB);
}

static void menu_render(stark_screen_t *self, gfx_surface_t *s)
{
    ui_menu_t *m = self->state;
    gfx_fill(s, list_rect(), UI_COLOR_BG);
    if (m->count == 0) {
        (void)gfx_text(s, &gfx_font_mono16, UI_MENU_TEXT_X, UI_STATUSBAR_H + UI_MENU_TEXT_Y,
                       "(empty)", UI_COLOR_DISABLED, UI_COLOR_BG, true);
        return;
    }
    size_t top = ui_menu_model_top(&m->model);
    size_t rows = visible_rows();
    for (size_t i = top; i < m->count && i < top + rows; i++) {
        gfx_rect_t r = row_rect(m, i);
        bool selected =
            ui_menu_model_has_selection(&m->model) && ui_menu_model_selected(&m->model) == i;
        uint16_t bg = selected ? UI_COLOR_SEL_BG : UI_COLOR_BG;
        uint16_t fg =
            selected ? UI_COLOR_SEL_FG : (m->items[i].disabled ? UI_COLOR_DISABLED : UI_COLOR_FG);
        if (selected) {
            gfx_fill(s, r, bg);
        }
        (void)gfx_text(s, &gfx_font_mono16, UI_MENU_TEXT_X, (int16_t)(r.y + UI_MENU_TEXT_Y),
                       m->items[i].label, fg, bg, true);
    }
    if (overflows(m)) {
        draw_scrollbar(m, s);
    }
}

/* After the model moved: repaint the two rows involved, or everything if
 * the window scrolled, and log the new selection. */
static void selection_changed(ui_menu_t *m, size_t old_sel, size_t old_top)
{
    size_t sel = ui_menu_model_selected(&m->model);
    if (ui_menu_model_top(&m->model) != old_top) {
        stark_ui_invalidate(&m->screen, list_rect());
    } else {
        stark_ui_invalidate(&m->screen, row_rect(m, old_sel));
        stark_ui_invalidate(&m->screen, row_rect(m, sel));
    }
    STARK_LOGI("menu", "sel=%u \"%s\"", (unsigned)sel, m->items[sel].label);
}

static bool menu_event(stark_screen_t *self, const stark_event_t *e)
{
    ui_menu_t *m = self->state;
    if (e->type != STARK_EVT_KEY) {
        return false;
    }
    bool press_or_repeat = e->key.action == STARK_KEY_PRESS || e->key.action == STARK_KEY_REPEAT;
    size_t old_sel = ui_menu_model_selected(&m->model);
    size_t old_top = ui_menu_model_top(&m->model);
    bool moved = false;

    switch ((stark_key_t)e->key.key) {
        case STARK_KEY_UP:
        case STARK_KEY_DOWN:
            if (!press_or_repeat) {
                return true;
            }
            moved = ui_menu_model_move(&m->model, e->key.key == STARK_KEY_UP ? -1 : +1);
            break;
        case STARK_KEY_LEFT:
        case STARK_KEY_RIGHT:
            if (!press_or_repeat) {
                return true;
            }
            moved = ui_menu_model_page(&m->model, e->key.key == STARK_KEY_LEFT ? -1 : +1);
            break;
        case STARK_KEY_OK:
            if (e->key.action == STARK_KEY_SHORT && ui_menu_model_has_selection(&m->model) &&
                m->on_activate != NULL) {
                m->on_activate(ui_menu_model_selected(&m->model), m->ctx);
            }
            return true;
        case STARK_KEY_BACK:
            return false; /* stark_ui pops */
    }
    if (moved) {
        selection_changed(m, old_sel, old_top);
    }
    return true;
}

void ui_menu_init(ui_menu_t *m, const char *title, const ui_menu_item_t *items, size_t count,
                  ui_menu_activate_fn on_activate, void *ctx)
{
    if (m == NULL) {
        return;
    }
    *m = (ui_menu_t){
        .screen = {.name = title, .on_event = menu_event, .on_render = menu_render, .state = m},
        .items = items,
        .count = items != NULL ? count : 0,
        .on_activate = on_activate,
        .ctx = ctx,
    };
    ui_menu_model_init(&m->model, m->count, visible_rows(), item_enabled, m);
}
