/*
 * ui_menu_model.c — see ui_menu_model.h. Pure C.
 */
#include "ui_menu_model.h"

static bool is_enabled(const ui_menu_model_t *m, size_t i)
{
    return m->enabled == NULL || m->enabled(i, m->ctx);
}

/* Keeps top such that top <= sel < top + visible and, for a list longer
 * than the window, top + visible <= count. When the selection sits on the
 * window's first row and the row above it is not selectable (a category
 * header), the window scrolls up one so that row stays in view (STARK-0107). */
static void follow(ui_menu_model_t *m)
{
    if (m->count <= m->visible) {
        m->top = 0;
        return;
    }
    if (m->sel < m->top) {
        m->top = m->sel;
    } else if (m->sel >= m->top + m->visible) {
        m->top = m->sel + 1 - m->visible;
    }
    if (m->top > m->count - m->visible) {
        m->top = m->count - m->visible;
    }
    if (m->visible >= 2 && m->sel == m->top && m->top > 0 && !is_enabled(m, m->top - 1)) {
        m->top--;
    }
}

/* Nearest enabled index from `from` (inclusive) stepping by +1/-1 without
 * wrapping; count if none. */
static size_t scan(const ui_menu_model_t *m, size_t from, int step)
{
    for (size_t i = from; i < m->count; i = step > 0 ? i + 1 : i - 1) {
        if (is_enabled(m, i)) {
            return i;
        }
        if (step < 0 && i == 0) {
            break;
        }
    }
    return m->count;
}

void ui_menu_model_init(ui_menu_model_t *m, size_t count, size_t visible,
                        ui_menu_enabled_fn enabled, void *ctx)
{
    if (m == NULL) {
        return;
    }
    *m = (ui_menu_model_t){
        .count = count,
        .visible = visible == 0 ? 1 : visible,
        .enabled = enabled,
        .ctx = ctx,
    };
    size_t first = scan(m, 0, +1);
    m->has_sel = first < count;
    m->sel = m->has_sel ? first : 0;
    follow(m);
}

bool ui_menu_model_move(ui_menu_model_t *m, int delta)
{
    if (m == NULL || !m->has_sel || delta == 0) {
        return false;
    }
    size_t i = m->sel;
    for (size_t n = 1; n < m->count; n++) {
        i = delta > 0 ? (i + 1) % m->count : (i + m->count - 1) % m->count;
        if (is_enabled(m, i)) {
            m->sel = i;
            follow(m);
            return true;
        }
    }
    return false; /* no other enabled item */
}

bool ui_menu_model_page(ui_menu_model_t *m, int dir)
{
    if (m == NULL || !m->has_sel || dir == 0) {
        return false;
    }
    size_t target;
    if (dir > 0) {
        target = m->count - 1 - m->sel < m->visible ? m->count - 1 : m->sel + m->visible;
    } else {
        target = m->sel < m->visible ? 0 : m->sel - m->visible;
    }
    size_t pick = scan(m, target, dir > 0 ? +1 : -1);
    if (pick == m->count) {
        pick = scan(m, target, dir > 0 ? -1 : +1); /* nothing further that way */
    }
    if (pick == m->count || pick == m->sel) {
        return false;
    }
    m->sel = pick;
    follow(m);
    return true;
}

bool ui_menu_model_has_selection(const ui_menu_model_t *m)
{
    return m != NULL && m->has_sel;
}

size_t ui_menu_model_selected(const ui_menu_model_t *m)
{
    return m != NULL ? m->sel : 0;
}

size_t ui_menu_model_top(const ui_menu_model_t *m)
{
    return m != NULL ? m->top : 0;
}
