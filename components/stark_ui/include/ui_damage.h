/*
 * ui_damage.h — a screen's damage as a small set of non-overlapping
 * rectangles (TASKS.md STARK-0101, ARCHITECTURE.md §6.7). Pure: no drawing,
 * no ESP-IDF; host-tested (test/host/test_ui_damage.c).
 *
 * Distant invalidations stay separate (a menu wrap repaints two rows, not
 * the list between them); overlapping or edge-sharing ones merge; when a
 * fifth distinct rect arrives, the pair whose union grows the covered area
 * least is merged. The set always covers every pixel ever added since the
 * last clear, and its rects never overlap, so no pixel is drawn twice.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stark_gfx.h"

#define UI_DAMAGE_MAX 4

/* Zero-initialised = empty. Read r[0 .. n-1]; change only through the calls. */
typedef struct {
    gfx_rect_t r[UI_DAMAGE_MAX];
    uint8_t n;
} ui_damage_t;

void ui_damage_clear(ui_damage_t *d);
bool ui_damage_empty(const ui_damage_t *d);

/*
 * Adds `a` (logical coordinates). Empty or negative-size rects are ignored.
 * `a` merges (bounding box) with every stored rect it overlaps or shares an
 * edge with, cascading until no two stored rects overlap or share an edge;
 * otherwise it is appended; beyond UI_DAMAGE_MAX, the pair — over the stored
 * rects and `a` — whose union adds the least area is merged (ties: the
 * lowest index pair, `a` counting as the last index).
 */
void ui_damage_add(ui_damage_t *d, gfx_rect_t a);
