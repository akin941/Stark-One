/*
 * ui_stack.c — screen stack, damage coalescing, event routing and the UI
 * loop (TASKS.md STARK-0017, ARCHITECTURE.md §6.7, §7).
 *
 * Only the UI task runs this code: it is the event bus's one consumer, so
 * event handlers, push/pop and rendering never race. No allocation.
 */
#include "stark_ui.h"
#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "stark_display.h"
#include "stark_hal.h"
#include "stark_input.h"
#include "stark_log.h"
#include "ui_internal.h"
#include "ui_theme.h"

#define UI_STACK_DEPTH 8

_Static_assert(CONFIG_STARK_UI_TARGET_FPS >= 1, "STARK_UI_TARGET_FPS must be at least 1");

static stark_screen_t *s_stack[UI_STACK_DEPTH];
static size_t s_depth;
static gfx_rect_t s_status_damage;
static bool s_ready;

/* ---- rectangles ------------------------------------------------------ */

static bool rect_empty(gfx_rect_t r)
{
    return r.w <= 0 || r.h <= 0;
}

static int32_t min32(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

static int32_t max32(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static gfx_rect_t rect_make(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (x0 >= x1 || y0 >= y1) {
        return (gfx_rect_t){0, 0, 0, 0};
    }
    return (gfx_rect_t){(int16_t)x0, (int16_t)y0, (int16_t)min32(x1 - x0, INT16_MAX),
                        (int16_t)min32(y1 - y0, INT16_MAX)};
}

static gfx_rect_t rect_union(gfx_rect_t a, gfx_rect_t b)
{
    if (rect_empty(a)) {
        return b;
    }
    if (rect_empty(b)) {
        return a;
    }
    return rect_make(min32(a.x, b.x), min32(a.y, b.y),
                     max32((int32_t)a.x + a.w, (int32_t)b.x + b.w),
                     max32((int32_t)a.y + a.h, (int32_t)b.y + b.h));
}

static gfx_rect_t rect_intersect(gfx_rect_t a, gfx_rect_t b)
{
    return rect_make(max32(a.x, b.x), max32(a.y, b.y),
                     min32((int32_t)a.x + a.w, (int32_t)b.x + b.w),
                     min32((int32_t)a.y + a.h, (int32_t)b.y + b.h));
}

static gfx_rect_t content_rect(void)
{
    return (gfx_rect_t){0, UI_STATUSBAR_H, stark_display_width(),
                        (int16_t)(stark_display_height() - UI_STATUSBAR_H)};
}

/* ---- stack ------------------------------------------------------------ */

static stark_screen_t *top(void)
{
    return s_depth > 0 ? s_stack[s_depth - 1] : NULL;
}

/* A screen that just became the top one: redraw all of it, title included. */
static void redraw_all(stark_screen_t *s)
{
    s->damage = content_rect();
    s_status_damage = ui_statusbar_rect();
}

void stark_ui_invalidate(stark_screen_t *s, gfx_rect_t area)
{
    if (s != NULL && !rect_empty(area)) {
        s->damage = rect_union(s->damage, area);
    }
}

stark_err_t stark_ui_push(stark_screen_t *screen)
{
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    if (screen == NULL || screen->on_render == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_depth == UI_STACK_DEPTH) {
        return STARK_ERR_NO_MEM;
    }
    s_stack[s_depth++] = screen;
    if (screen->on_enter != NULL) {
        screen->on_enter(screen);
    }
    redraw_all(screen);
    STARK_LOGD("ui", "push \"%s\" depth=%u", screen->name, (unsigned)s_depth);
    return STARK_OK;
}

stark_err_t stark_ui_pop(void)
{
    if (!s_ready || s_depth <= 1) {
        return STARK_ERR_STATE; /* never pop the root */
    }
    stark_screen_t *leaving = s_stack[--s_depth];
    s_stack[s_depth] = NULL;
    if (leaving->on_exit != NULL) {
        leaving->on_exit(leaving);
    }
    redraw_all(top());
    STARK_LOGD("ui", "pop \"%s\" depth=%u", leaving->name, (unsigned)s_depth);
    return STARK_OK;
}

/* ---- events ----------------------------------------------------------- */

static void on_bus_event(const stark_event_t *e, void *ctx)
{
    (void)ctx;
    stark_screen_t *t = top();
    if (t == NULL) {
        return;
    }
    if (t->on_event != NULL && t->on_event(t, e)) {
        return;
    }
    /* Global navigation (ARCHITECTURE §6.7): an unconsumed BACK leaves the
     * screen; at the root it is a no-op — the short buzz arrives with
     * stark_buzzer. */
    if (e->type == STARK_EVT_KEY && e->key.key == STARK_KEY_BACK &&
        e->key.action == STARK_KEY_SHORT && stark_ui_pop() != STARK_OK) {
        STARK_LOGD("ui", "back at root");
    }
}

stark_err_t stark_ui_init(void)
{
    if (s_ready) {
        return STARK_ERR_STATE;
    }
    STARK_CHECK_RET(stark_event_subscribe(UINT32_MAX, on_bus_event, NULL));
    s_ready = true;
    return STARK_OK;
}

/* ---- rendering -------------------------------------------------------- */

static void render_band(gfx_surface_t *s, void *ctx)
{
    stark_screen_t *screen = ctx;
    ui_statusbar_draw(s, screen->name); /* clipped away unless this band holds it */

    /* The screen draws below the status bar only. */
    gfx_rect_t full_clip = s->clip;
    s->clip = rect_intersect(full_clip, content_rect());
    if (!rect_empty(s->clip)) {
        screen->on_render(screen, s);
    }
    s->clip = full_clip;
}

void stark_ui_tick(void)
{
    if (!s_ready) {
        return;
    }
    (void)stark_event_dispatch(UINT32_MAX);

    stark_screen_t *screen = top();
    if (screen == NULL) {
        return;
    }
    gfx_rect_t area = rect_union(screen->damage, s_status_damage);
    if (rect_empty(area)) {
        return;
    }
    /* Cleared first, so on_render may already invalidate the next frame. */
    screen->damage = (gfx_rect_t){0, 0, 0, 0};
    s_status_damage = (gfx_rect_t){0, 0, 0, 0};

    STARK_LOGD("ui", "render %d,%d %dx%d", area.x, area.y, area.w, area.h);
    stark_err_t err = stark_display_render(area, render_band, screen);
    if (err != STARK_OK) {
        STARK_LOGE("ui", "render failed: %s", stark_err_str(err));
    }
}

void stark_ui_task(void *arg)
{
    (void)arg;
    const uint32_t frame_ms = 1000u / CONFIG_STARK_UI_TARGET_FPS;
    for (;;) {
        uint64_t start_us = stark_hal_now_us();
        stark_ui_tick();
        uint32_t spent_ms = (uint32_t)((stark_hal_now_us() - start_us) / 1000u);
        /* Sleep for the rest of the frame unless an event arrives first. */
        (void)stark_event_wait(spent_ms < frame_ms ? frame_ms - spent_ms : 0);
    }
}
