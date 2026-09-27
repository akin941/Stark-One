/*
 * ui_stack.c — screen stack, damage coalescing, event routing and the UI
 * loop (TASKS.md STARK-0017, ARCHITECTURE.md §6.7, §7).
 *
 * Only the UI task runs this code: it is the event bus's one consumer, so
 * event handlers, push/pop and rendering never race. No allocation.
 */
#include "stark_ui.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "stark_buzzer.h"
#include "stark_display.h"
#include "stark_hal.h"
#include "stark_input.h"
#include "stark_log.h"
#include "ui_internal.h"
#include "stark_theme.h"

#define UI_STACK_DEPTH 8

_Static_assert(CONFIG_STARK_UI_TARGET_FPS >= 1, "STARK_UI_TARGET_FPS must be at least 1");

#define UI_FRAME_BUDGET_US (1000000u / (uint32_t)CONFIG_STARK_UI_TARGET_FPS)

static stark_screen_t *s_stack[UI_STACK_DEPTH];
static size_t s_depth;
static gfx_rect_t s_status_damage;
static bool s_ready;

/* Frame statistics: written only by the UI task, readable from any task. */
static atomic_uint_least32_t s_frames;
static atomic_uint_least32_t s_renders;
static atomic_uint_least32_t s_overruns;
static atomic_uint_least32_t s_render_errors;
static atomic_uint_least32_t s_last_frame_us;
static atomic_uint_least32_t s_max_frame_us;

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

static gfx_rect_t rect_intersect(gfx_rect_t a, gfx_rect_t b)
{
    return rect_make(max32(a.x, b.x), max32(a.y, b.y),
                     min32((int32_t)a.x + a.w, (int32_t)b.x + b.w),
                     min32((int32_t)a.y + a.h, (int32_t)b.y + b.h));
}

static gfx_rect_t screen_rect(void)
{
    return (gfx_rect_t){0, 0, stark_display_width(), stark_display_height()};
}

gfx_rect_t stark_ui_content_rect(void)
{
    return (gfx_rect_t){0, STARK_THEME_STATUSBAR_H, stark_display_width(),
                        (int16_t)(stark_display_height() - STARK_THEME_STATUSBAR_H)};
}

/* ---- stack ------------------------------------------------------------ */

static stark_screen_t *top(void)
{
    return s_depth > 0 ? s_stack[s_depth - 1] : NULL;
}

/* A screen that just became the top one: redraw all of it, title included. */
static void redraw_all(stark_screen_t *s)
{
    ui_damage_clear(&s->damage);
    ui_damage_add(&s->damage, stark_ui_content_rect());
    s_status_damage = ui_statusbar_rect();
}

void stark_ui_invalidate(stark_screen_t *s, gfx_rect_t area)
{
    if (s != NULL) {
        ui_damage_add(&s->damage, area);
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
    if (screen->overlay && s_depth == 0) {
        return STARK_ERR_STATE; /* an overlay needs a screen beneath it */
    }
    if (screen->overlay && top()->overlay) {
        return STARK_ERR_BUSY; /* one overlay level only */
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

stark_err_t stark_ui_pop_to_root(void)
{
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    unsigned popped = 0;
    while (s_depth > 1) {
        (void)stark_ui_pop(); /* each screen's normal exit: on_stop, dialog CANCEL */
        popped++;
    }
    if (popped > 0) {
        STARK_LOGI("ui", "home depth=%u", popped);
    } else {
        STARK_LOGI("ui", "home at root");
        stark_buzzer_reject();
    }
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
    /* Global navigation first, before any screen can consume it
     * (ARCHITECTURE §6.7): OK+BACK is reserved for a future soft reset, and
     * a BACK held long goes home from anywhere (STARK-0106). */
    if (e->type == STARK_EVT_KEY && e->key.action == STARK_KEY_CHORD) {
        STARK_LOGI("ui", "chord reserved");
        return;
    }
    if (e->type == STARK_EVT_KEY && e->key.key == STARK_KEY_BACK &&
        e->key.action == STARK_KEY_LONG) {
        (void)stark_ui_pop_to_root();
        return;
    }
    if (t->on_event != NULL && t->on_event(t, e)) {
        return;
    }
    /* Global navigation (ARCHITECTURE §6.7): an unconsumed BACK leaves the
     * screen; at the root it is a no-op with a short low buzz. */
    if (e->type == STARK_EVT_KEY && e->key.key == STARK_KEY_BACK &&
        e->key.action == STARK_KEY_SHORT && stark_ui_pop() != STARK_OK) {
        STARK_LOGD("ui", "back at root");
        stark_buzzer_reject();
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
    /* Under an overlay, the screen beneath owns the title and is drawn first. */
    stark_screen_t *beneath = screen->overlay && s_depth >= 2 ? s_stack[s_depth - 2] : NULL;
    ui_statusbar_draw(s, beneath != NULL ? beneath->name : screen->name); /* clipped away
                                                                           * unless this band
                                                                           * holds it */

    /* Screens draw below the status bar only. */
    gfx_rect_t full_clip = s->clip;
    s->clip = rect_intersect(full_clip, stark_ui_content_rect());
    if (!rect_empty(s->clip)) {
        if (beneath != NULL) {
            beneath->on_render(beneath, s);
        }
        screen->on_render(screen, s);
    }
    s->clip = full_clip;
}

static void count(atomic_uint_least32_t *c)
{
    atomic_store_explicit(c, atomic_load_explicit(c, memory_order_relaxed) + 1u,
                          memory_order_relaxed);
}

static void note_frame(uint32_t us)
{
    count(&s_frames);
    atomic_store_explicit(&s_last_frame_us, us, memory_order_relaxed);
    if (us > atomic_load_explicit(&s_max_frame_us, memory_order_relaxed)) {
        atomic_store_explicit(&s_max_frame_us, us, memory_order_relaxed);
    }
    if (us > UI_FRAME_BUDGET_US) {
        count(&s_overruns);
        STARK_LOGD("ui", "overrun %u us", (unsigned)us);
    }
}

void stark_ui_tick(void)
{
    if (!s_ready) {
        return;
    }
    uint64_t start_us = stark_hal_now_us();
    (void)stark_event_dispatch(UINT32_MAX);

    stark_screen_t *screen = top();
    if (screen == NULL) {
        return;
    }
    ui_damage_t frame = screen->damage;
    ui_damage_add(&frame, s_status_damage);
    if (ui_damage_empty(&frame)) {
        return;
    }
    /* Cleared first, so on_render may already invalidate the next frame. */
    ui_damage_clear(&screen->damage);
    s_status_damage = (gfx_rect_t){0, 0, 0, 0};

    for (uint8_t i = 0; i < frame.n; i++) {
        gfx_rect_t area = rect_intersect(frame.r[i], screen_rect());
        if (rect_empty(area)) {
            continue;
        }
        STARK_LOGD("ui", "render %d,%d %dx%d", area.x, area.y, area.w, area.h);
        count(&s_renders);
        stark_err_t err = stark_display_render(area, render_band, screen);
        if (err != STARK_OK) {
            count(&s_render_errors);
            STARK_LOGE("ui", "render failed: %s", stark_err_str(err));
        }
    }
    uint64_t spent_us = stark_hal_now_us() - start_us;
    note_frame(spent_us > UINT32_MAX ? UINT32_MAX : (uint32_t)spent_us);
}

stark_err_t stark_ui_stats(stark_ui_stats_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    *out = (stark_ui_stats_t){
        .frames = atomic_load_explicit(&s_frames, memory_order_relaxed),
        .renders = atomic_load_explicit(&s_renders, memory_order_relaxed),
        .overruns = atomic_load_explicit(&s_overruns, memory_order_relaxed),
        .render_errors = atomic_load_explicit(&s_render_errors, memory_order_relaxed),
        .last_frame_us = atomic_load_explicit(&s_last_frame_us, memory_order_relaxed),
        .max_frame_us = atomic_load_explicit(&s_max_frame_us, memory_order_relaxed),
    };
    return STARK_OK;
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
