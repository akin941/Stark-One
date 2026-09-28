/*
 * app_diagnostics.c — Diagnostics (TASKS.md STARK-0109): stark_diag's
 * snapshot and task table in the 6x10 font, refreshed on every diag sample
 * (STARK_SYS_DIAG_SAMPLE, once a second); only rows whose text changed are
 * redrawn. Each refresh logs the heap and FPS it drew. FPS and overruns are
 * reported, never gated in simulation (ADR-0011).
 */
#include <stdio.h>
#include <string.h>
#include "gfx_font.h"
#include "stark_app.h"
#include "stark_diag.h"
#include "stark_log.h"
#include "stark_theme.h"

#define ROWS      18
#define ROW_PITCH 12 /* 10 px cells + 2 */
#define ROW_CHARS 48
#define TASK_ROW0 12
#define MAX_TASKS (ROWS - TASK_ROW0)

static char s_rows[ROWS][ROW_CHARS];
static stark_screen_t s_screen;

static int16_t row_y(int i)
{
    return (int16_t)(stark_ui_content_rect().y + 4 + i * ROW_PITCH);
}

/* Fills `rows` from the current snapshot and task table. */
static void format_rows(char rows[ROWS][ROW_CHARS])
{
    stark_diag_snapshot_t d = {0};
    (void)stark_diag_snapshot(&d);
    memset(rows, 0, ROWS * ROW_CHARS);
    snprintf(rows[0], ROW_CHARS, "Heap free   %lu B", (unsigned long)d.heap_free);
    snprintf(rows[1], ROW_CHARS, "Heap min    %lu B", (unsigned long)d.heap_min);
    snprintf(rows[2], ROW_CHARS, "Largest     %lu B", (unsigned long)d.heap_largest);
    snprintf(rows[3], ROW_CHARS, "FPS         %lu.%lu", (unsigned long)(d.fps_x10 / 10),
             (unsigned long)(d.fps_x10 % 10));
    snprintf(rows[4], ROW_CHARS, "Frames      %lu", (unsigned long)d.ui.frames);
    snprintf(rows[5], ROW_CHARS, "Overruns    %lu", (unsigned long)d.ui.overruns);
    snprintf(rows[6], ROW_CHARS, "Render err  %lu", (unsigned long)d.ui.render_errors);
    snprintf(rows[7], ROW_CHARS, "Join t/o    %lu", (unsigned long)d.ui.join_timeouts);
    snprintf(rows[8], ROW_CHARS, "Events      %lu pub %lu drop %lu peak",
             (unsigned long)d.ev_published, (unsigned long)d.ev_dropped,
             (unsigned long)d.ev_max_depth);
    snprintf(rows[9], ROW_CHARS, "Uptime      %lu s", (unsigned long)d.uptime_s);
    snprintf(rows[11], ROW_CHARS, "Task         stack free (B)");
    stark_diag_task_t tasks[MAX_TASKS];
    size_t n = stark_diag_tasks(tasks, MAX_TASKS);
    for (size_t i = 0; i < n; i++) {
        snprintf(rows[TASK_ROW0 + i], ROW_CHARS, "%-12.12s %lu", tasks[i].name,
                 (unsigned long)tasks[i].stack_free);
    }
    STARK_LOGI("diagnostics", "heap=%lu fps=%lu.%lu", (unsigned long)d.heap_free,
               (unsigned long)(d.fps_x10 / 10), (unsigned long)(d.fps_x10 % 10));
}

static bool diag_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type != STARK_EVT_SYSTEM || e->system.id != STARK_SYS_DIAG_SAMPLE) {
        return false; /* BACK belongs to stark_ui */
    }
    char next[ROWS][ROW_CHARS];
    format_rows(next);
    int16_t w = stark_ui_content_rect().w;
    for (int i = 0; i < ROWS; i++) {
        if (strcmp(next[i], s_rows[i]) != 0) {
            memcpy(s_rows[i], next[i], ROW_CHARS);
            stark_ui_invalidate(self, (gfx_rect_t){0, row_y(i), w, ROW_PITCH});
        }
    }
    return true;
}

static void diag_render(stark_screen_t *self, gfx_surface_t *s)
{
    (void)self;
    gfx_fill(s, s->clip, STARK_THEME_BG);
    for (int i = 0; i < ROWS; i++) {
        uint16_t fg = i == 3 || i == 0 ? STARK_THEME_ACTIVE : STARK_THEME_FG; /* heap, FPS */
        (void)gfx_text(s, &gfx_font_mono10, STARK_THEME_TEXT_X, row_y(i), s_rows[i], fg,
                       STARK_THEME_BG, true);
    }
}

static stark_screen_t *diag_screen(void *state)
{
    (void)state;
    format_rows(s_rows);
    s_screen =
        (stark_screen_t){.name = "Diagnostics", .on_event = diag_event, .on_render = diag_render};
    return &s_screen;
}

/* Launcher icon (16x16, 1bpp — ui_menu.h): a pulse trace. */
static const uint8_t k_icon[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x40, 0x00, 0xA0, 0x00, 0xA0, 0x01, 0x10,
    0xF1, 0x1F, 0x0A, 0x08, 0x0A, 0x08, 0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

const stark_app_t app_diagnostics = {
    .id = "diagnostics",
    .title = "Diagnostics",
    .category = "System",
    .screen = diag_screen,
    .icon = k_icon,
};
