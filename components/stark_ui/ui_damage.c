/*
 * ui_damage.c — the damage set (ui_damage.h). 32-bit arithmetic throughout,
 * like gfx_clip(), so edges past INT16_MAX cannot wrap.
 */
#include "ui_damage.h"
#include <stddef.h>

typedef struct {
    int32_t x0, y0, x1, y1; /* half-open: [x0, x1) x [y0, y1) */
} box_t;

static box_t to_box(gfx_rect_t r)
{
    return (box_t){r.x, r.y, (int32_t)r.x + r.w, (int32_t)r.y + r.h};
}

static gfx_rect_t to_rect(box_t b)
{
    int32_t w = b.x1 - b.x0;
    int32_t h = b.y1 - b.y0;
    return (gfx_rect_t){(int16_t)b.x0, (int16_t)b.y0, (int16_t)(w > INT16_MAX ? INT16_MAX : w),
                        (int16_t)(h > INT16_MAX ? INT16_MAX : h)};
}

static box_t box_union(box_t a, box_t b)
{
    return (box_t){a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
                   a.y1 > b.y1 ? a.y1 : b.y1};
}

static int64_t box_area(box_t b)
{
    return (int64_t)(b.x1 - b.x0) * (int64_t)(b.y1 - b.y0);
}

/* Overlap, or a shared edge: strictly overlapping on one axis and touching
 * or overlapping on the other. Corner-only contact does not count. */
static bool joins(box_t a, box_t b)
{
    bool x_overlap = a.x0 < b.x1 && b.x0 < a.x1;
    bool y_overlap = a.y0 < b.y1 && b.y0 < a.y1;
    bool x_touch = a.x0 <= b.x1 && b.x0 <= a.x1;
    bool y_touch = a.y0 <= b.y1 && b.y0 <= a.y1;
    return (x_overlap && y_touch) || (y_overlap && x_touch);
}

void ui_damage_clear(ui_damage_t *d)
{
    d->n = 0;
}

bool ui_damage_empty(const ui_damage_t *d)
{
    return d->n == 0;
}

static void remove_at(ui_damage_t *d, size_t i)
{
    for (size_t k = i + 1; k < d->n; k++) {
        d->r[k - 1] = d->r[k];
    }
    d->n--;
}

/* Merges b into d until nothing joins it; returns the merged box, which
 * d does not yet contain. */
static box_t absorb(ui_damage_t *d, box_t b)
{
    size_t i = 0;
    while (i < d->n) {
        box_t s = to_box(d->r[i]);
        if (joins(s, b)) {
            b = box_union(s, b);
            remove_at(d, i);
            i = 0; /* the grown box may now join an earlier rect */
        } else {
            i++;
        }
    }
    return b;
}

void ui_damage_add(ui_damage_t *d, gfx_rect_t a)
{
    if (a.w <= 0 || a.h <= 0) {
        return;
    }
    box_t b = absorb(d, to_box(a));
    if (d->n < UI_DAMAGE_MAX) {
        d->r[d->n++] = to_rect(b);
        return;
    }

    /* Full: merge the cheapest pair among the stored rects and b. */
    box_t all[UI_DAMAGE_MAX + 1];
    for (size_t k = 0; k < UI_DAMAGE_MAX; k++) {
        all[k] = to_box(d->r[k]);
    }
    all[UI_DAMAGE_MAX] = b;
    size_t bi = 0;
    size_t bj = 1;
    int64_t best = INT64_MAX;
    for (size_t i = 0; i < UI_DAMAGE_MAX + 1; i++) {
        for (size_t j = i + 1; j < UI_DAMAGE_MAX + 1; j++) {
            int64_t growth =
                box_area(box_union(all[i], all[j])) - box_area(all[i]) - box_area(all[j]);
            if (growth < best) {
                best = growth;
                bi = i;
                bj = j;
            }
        }
    }
    box_t merged = box_union(all[bi], all[bj]);
    d->n = 0;
    for (size_t k = 0; k < UI_DAMAGE_MAX + 1; k++) {
        if (k != bi && k != bj) {
            d->r[d->n++] = to_rect(all[k]);
        }
    }
    b = absorb(d, merged);
    d->r[d->n++] = to_rect(b);
}
