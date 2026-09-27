/*
 * app_inputtest.c — Input Test (TASKS.md STARK-0020): each key's state, its
 * last action and a press counter, live. Only the changed key's row is
 * redrawn. BACK still exits (after its row shows the press).
 */
#include <stdio.h>
#include "gfx_font.h"
#include "stark_app.h"
#include "stark_input.h"
#include "stark_theme.h"

#define ROW_Y(k) (int16_t)(stark_ui_content_rect().y + 8 + (k) * 28)
#define ROW_H    28

static const char *const k_names[STARK_KEY_COUNT] = {"UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK"};
static const char *const k_actions[] = {"press", "release", "repeat", "long", "short"};

static bool s_down[STARK_KEY_COUNT];
static int s_last[STARK_KEY_COUNT]; /* stark_key_action_t, or -1 */
static unsigned s_presses[STARK_KEY_COUNT];
static stark_screen_t s_screen;

static bool input_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type != STARK_EVT_KEY || e->key.key >= STARK_KEY_COUNT) {
        return false;
    }
    int k = e->key.key;
    s_last[k] = e->key.action;
    if (e->key.action == STARK_KEY_PRESS) {
        s_down[k] = true;
        s_presses[k]++;
    } else if (e->key.action == STARK_KEY_RELEASE) {
        s_down[k] = false;
    }
    stark_ui_invalidate(self, (gfx_rect_t){0, ROW_Y(k), stark_ui_content_rect().w, ROW_H});
    /* BACK short is left unconsumed so stark_ui still pops the app. */
    return !(k == STARK_KEY_BACK && e->key.action == STARK_KEY_SHORT);
}

static void input_render(stark_screen_t *self, gfx_surface_t *s)
{
    (void)self;
    gfx_fill(s, s->clip, STARK_THEME_BG);
    for (int k = 0; k < STARK_KEY_COUNT; k++) {
        char line[48];
        snprintf(line, sizeof line, "%-6s %-4s %-8s n=%u", k_names[k], s_down[k] ? "DOWN" : "up",
                 s_last[k] >= 0 ? k_actions[s_last[k]] : "-", s_presses[k]);
        uint16_t fg = s_down[k] ? STARK_THEME_ACTIVE : STARK_THEME_FG;
        (void)gfx_text(s, &gfx_font_mono16, STARK_THEME_TEXT_X, (int16_t)(ROW_Y(k) + 6), line, fg,
                       STARK_THEME_BG, true);
    }
}

static stark_err_t input_start(void **state)
{
    (void)state;
    for (int k = 0; k < STARK_KEY_COUNT; k++) {
        s_down[k] = false;
        s_last[k] = -1;
        s_presses[k] = 0;
    }
    return STARK_OK;
}

static stark_screen_t *input_screen(void *state)
{
    (void)state;
    s_screen =
        (stark_screen_t){.name = "Input Test", .on_event = input_event, .on_render = input_render};
    return &s_screen;
}

const stark_app_t app_inputtest = {
    .id = "inputtest",
    .title = "Input Test",
    .category = "System",
    .on_start = input_start,
    .screen = input_screen,
};
