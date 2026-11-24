/*
 * app_selftest.c — Self-Test (TASKS.md STARK-0707, ADR-0023 §8). Home: Run all, a summary line
 * (`7 pass  3 attested  0 fail  0 skip`), one row per check with its result, Export report. Run
 * all executes the automated checks on the app worker (RAM, flash, settings, storage, module bus,
 * events and stacks), then walks the operator through what the device cannot judge itself:
 * Buttons (the input service's own key events are the observation), Display, Buzzer and Status
 * LED; BACK on a question records `fail` and ends the walk, the rest stays `not run`. What the
 * operator confirms is `attested`, never `pass`. OK on a check row shows its evidence. Export
 * report writes logs/report_NNNN.txt through the root (the worker blocks on the card); without a
 * card it says so and logs `selftest: export skipped (no card)`. Everything stays on the device.
 *
 * The checks and the table are the composition root's (main/selftest_checks.c); stark_diag runs
 * them and keeps the results. This file binds them to the worker and the screens; the rows and the
 * walk are selftest_views.c (pure).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx_font.h"
#include "selftest_report.h"
#include "selftestapp.h"
#include "stark_app.h"
#include "stark_buzzer.h"
#include "stark_diag.h"
#include "stark_event.h"
#include "stark_hal.h"
#include "stark_input.h"
#include "stark_log.h"
#include "stark_theme.h"
#include "ui_dialog.h"
#include "ui_menu.h"

#define NOTE_AUTOMATED_DONE                                                                        \
    400                      /* STARK_APP_REQ_NOTIFY arg: the worker has run the automated checks */
#define NOTE_EXPORT_DONE 401 /* ... the worker has written (or failed to write) the report */
#define NO_SEL           SIZE_MAX

typedef enum { BUSY_NONE, BUSY_RUN, BUSY_EXPORT } busy_t;

/* Everything the menu points into, held on the heap while the app runs (about 1.2 kB) and given
 * back by on_stop(): nothing here costs the boot heap while no one is testing. */
typedef struct {
    ui_menu_item_t items[STAPP_ROWS_MAX];
    strow_t rows[STAPP_ROWS_MAX];
    stark_selftest_row_t results[STARK_SELFTEST_MAX];
    char summary[64];
} st_state_t;

static st_state_t *s_st;
static ui_menu_t s_menu;
static bool (*s_menu_event)(stark_screen_t *self, const stark_event_t *e);
static size_t s_row_count;
static size_t s_check_count;
static busy_t s_busy;

/* The export's answer, written by the worker and read after its notification. */
static struct {
    stark_err_t err;
    char name[SELFTEST_REPORT_NAME_MAX];
    size_t bytes;
} s_export;

static stark_screen_t s_walk_screen;
static stwalk_t s_walk;
static stwalk_step_t
    s_shown; /* the step the walk screen draws (the walk itself is NONE once over) */
static bool s_walk_open;

static const stark_buzzer_note_t k_tones[] = {{523, 200}, {659, 200}, {784, 200}};

static bool self_event(stark_screen_t *self, const stark_event_t *e);
static void self_exit(stark_screen_t *self);
static void activate(size_t index, void *ctx);

static uint32_t now_ms(void)
{
    return (uint32_t)(stark_hal_now_us() / 1000u);
}

/* ---- the home list ------------------------------------------------------------------------ */

static void refresh(void)
{
    size_t total = stark_selftest_results(s_st->results, STARK_SELFTEST_MAX);
    s_check_count = total < STARK_SELFTEST_MAX ? total : STARK_SELFTEST_MAX;
    stview_summary(s_st->results, s_check_count, s_st->summary, sizeof s_st->summary);
    s_row_count = stview_rows(s_check_count, s_st->rows);
    for (size_t i = 0; i < s_row_count; i++) {
        s_st->items[i] = (ui_menu_item_t){
            .label = stview_label(&s_st->rows[i], s_st->results, s_check_count, s_st->summary),
            .disabled = s_st->rows[i].kind == STROW_SUMMARY};
    }
}

static const char *value_of(size_t index, void *ctx)
{
    (void)ctx;
    return index < s_row_count ? stview_value(&s_st->rows[index], s_st->results, s_check_count)
                               : NULL;
}

/* Moves the selection to `want` (the menu re-initialises its model with the first row selected). */
static void reselect(size_t want)
{
    for (size_t guard = 0;
         guard < s_row_count && want != NO_SEL && ui_menu_model_has_selection(&s_menu.model) &&
         ui_menu_model_selected(&s_menu.model) != want;
         guard++) {
        (void)ui_menu_model_move(&s_menu.model, 1);
    }
}

/*
 * (Re)builds the one menu from the results as they are now. NOTE: the app manager hooks the
 * running screen's on_exit to stop the app (app_manager.c); ui_menu_init() rewrites the whole
 * screen, so the hook is carried over, and the first call installs self_exit() as the screen's own,
 * which the manager keeps.
 */
static void show(size_t sel)
{
    void (*on_exit)(stark_screen_t *) =
        s_menu.screen.on_exit != NULL ? s_menu.screen.on_exit : self_exit;
    refresh();
    ui_menu_init(&s_menu, "Self-Test", s_st->items, s_row_count, activate, NULL);
    s_menu.value = value_of;
    s_menu_event = s_menu.screen.on_event;
    s_menu.screen.on_event = self_event;
    s_menu.screen.on_exit = on_exit;
    reselect(sel);
    stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
}

static size_t selected(void)
{
    return ui_menu_model_has_selection(&s_menu.model) ? ui_menu_model_selected(&s_menu.model)
                                                      : NO_SEL;
}

static void alert(const char *title, const char *text)
{
    (void)stark_ui_dialog_alert(title, text, NULL, NULL);
}

/* ---- the operator walk -------------------------------------------------------------------- */

static void play_tones(void)
{
    (void)stark_buzzer_play(k_tones, sizeof k_tones / sizeof k_tones[0]);
}

static void walk_render(stark_screen_t *self, gfx_surface_t *s)
{
    (void)self;
    static const uint16_t k_bars[8] = {
        GFX_RGB565(255, 0, 0),     GFX_RGB565(0, 255, 0),   GFX_RGB565(0, 0, 255),
        GFX_RGB565(255, 255, 0),   GFX_RGB565(0, 255, 255), GFX_RGB565(255, 0, 255),
        GFX_RGB565(255, 255, 255), GFX_RGB565(0, 0, 0),
    };
    const gfx_rect_t c = stark_ui_content_rect();
    const int16_t x = STARK_THEME_TEXT_X;
    gfx_fill(s, s->clip, STARK_THEME_BG);
    int16_t y = c.y;
    if (s_shown == STWALK_DISPLAY) {
        /* The pattern is this step's subject: colour bars, then a grey ramp, edge to edge. */
        const int16_t bar_w = (int16_t)(c.w / 8);
        for (int16_t i = 0; i < 8; i++) {
            gfx_fill(s, (gfx_rect_t){(int16_t)(i * bar_w), c.y, bar_w, 64}, k_bars[i]);
        }
        for (int16_t px = 0; px < c.w; px++) {
            uint8_t v = (uint8_t)(px * 255 / (c.w - 1));
            gfx_vline(s, px, (int16_t)(c.y + 64), 48, GFX_RGB565(v, v, v));
        }
        y = (int16_t)(c.y + 128);
    }
    (void)gfx_text(s, &gfx_font_mono16, x, (int16_t)(y + 4), stview_step_title(s_shown),
                   STARK_THEME_ACCENT, STARK_THEME_BG, true);
    (void)gfx_text(s, &gfx_font_mono16, x, (int16_t)(y + 28), stview_step_text(s_shown),
                   STARK_THEME_FG, STARK_THEME_BG, true);
    if (s_shown == STWALK_BUTTONS) {
        char prompt[40];
        stview_buttons_prompt(&s_walk, now_ms(), prompt, sizeof prompt);
        (void)gfx_text(s, &gfx_font_mono16, x, (int16_t)(y + 52), prompt, STARK_THEME_ACTIVE,
                       STARK_THEME_BG, true);
        for (uint8_t k = 0; k < STAPP_KEYS; k++) {
            bool seen = (s_walk.seen & (1u << k)) != 0u;
            char line[24];
            (void)snprintf(line, sizeof line, "%-6s %s", stview_key_name(k), seen ? "ok" : "...");
            (void)gfx_text(s, &gfx_font_mono16, x, (int16_t)(y + 80 + k * 20), line,
                           seen ? STARK_THEME_ACTIVE : STARK_THEME_DISABLED, STARK_THEME_BG, true);
        }
    } else {
        (void)gfx_text(s, &gfx_font_mono16, x, (int16_t)(y + 56), stview_step_keys(s_shown),
                       STARK_THEME_ACTIVE, STARK_THEME_BG, true);
    }
}

static void walk_exit(stark_screen_t *self)
{
    (void)self;
    s_walk_open = false;
    s_walk.step = STWALK_NONE;
    stark_buzzer_stop();
}

/* Records what the walk decided and moves the screen on. */
static void walk_apply(stark_screen_t *self, const stwalk_out_t *out)
{
    if (out->have) {
        stark_selftest_report(out->index, out->result, out->evidence);
    }
    if (out->entered) {
        s_shown = s_walk.step;
        if (s_shown == STWALK_BUZZER) {
            play_tones();
        } else {
            stark_buzzer_stop();
        }
    }
    if (out->replay) {
        play_tones();
    }
    if (out->finished) {
        STARK_LOGI("selftestapp", "walk %s",
                   out->have && out->result == ST_FAIL ? "ended" : "done");
        (void)stark_ui_pop(); /* walk_exit() stops the buzzer */
        show(selected());
        return;
    }
    stark_ui_invalidate(self, stark_ui_content_rect());
}

static bool walk_event(stark_screen_t *self, const stark_event_t *e)
{
    stwalk_out_t out;
    memset(&out, 0, sizeof out);
    if (e->type == STARK_EVT_KEY) {
        stwalk_key(&s_walk, e->key.key, e->key.action, &out);
    } else if (e->type == STARK_EVT_SYSTEM && e->system.id == STARK_SYS_DIAG_SAMPLE) {
        stwalk_tick(&s_walk, now_ms(), &out); /* once a second: the 30 s of the Buttons step */
        if (s_walk.step == STWALK_BUTTONS) {
            stark_ui_invalidate(self, stark_ui_content_rect()); /* the seconds left */
        }
    }
    if (out.consumed || out.have || out.entered || out.finished) {
        walk_apply(self, &out);
    }
    return out.consumed;
}

static void start_walk(void)
{
    if (s_check_count <= (size_t)STAPP_LED || s_walk_open) {
        return;
    }
    stwalk_start(&s_walk, now_ms());
    s_shown = STWALK_BUTTONS;
    s_walk_screen = (stark_screen_t){.name = "Self-Test",
                                     .on_event = walk_event,
                                     .on_exit = walk_exit,
                                     .on_render = walk_render};
    if (stark_ui_push(&s_walk_screen) != STARK_OK) {
        s_walk.step = STWALK_NONE;
        return;
    }
    s_walk_open = true;
    STARK_LOGI("selftestapp", "walk start");
}

/* ---- the workers -------------------------------------------------------------------------- */

static void notify(int note)
{
    stark_event_t e = {.type = STARK_EVT_APP_REQUEST};
    e.app.id = STARK_APP_REQ_NOTIFY;
    e.app.arg = note;
    (void)stark_event_publish(&e);
}

/* The automated checks, in the table's order; BACK (the manager's stop request) cuts it short. */
static void run_worker(void *ctx)
{
    (void)ctx;
    for (size_t i = 0; i < (size_t)STAPP_BUTTONS && !stark_app_worker_should_stop(); i++) {
        (void)stark_selftest_run(i);
    }
    notify(NOTE_AUTOMATED_DONE);
}

static void export_worker(void *ctx)
{
    (void)ctx;
    s_export.bytes = 0;
    s_export.err = stark_selftest_export(s_export.name, sizeof s_export.name, &s_export.bytes);
    notify(NOTE_EXPORT_DONE);
}

static void start_job(busy_t what, stark_app_worker_fn fn)
{
    stark_err_t err = stark_app_worker_start(fn, NULL);
    if (err != STARK_OK) {
        STARK_LOGW("selftestapp", "worker not started: %s", stark_err_str(err));
        alert("Self-Test", "Could not start.");
        return;
    }
    s_busy = what;
}

static void run_all(void)
{
    if (s_check_count == 0u) {
        alert("Self-Test", "No checks are registered.");
        return;
    }
    STARK_LOGI("selftestapp", "run all");
    for (size_t i = 0; i < s_check_count; i++) {
        stark_selftest_report(i, ST_NOT_RUN, ""); /* a clear is silent: no event, no log */
    }
    show(selected());
    start_job(BUSY_RUN, run_worker);
}

static void export_report(void)
{
    STARK_LOGI("selftestapp", "export");
    start_job(BUSY_EXPORT, export_worker);
}

static void export_done(void)
{
    s_busy = BUSY_NONE;
    if (s_export.err == STARK_OK) {
        STARK_LOGI("selftest", "export %s %lu B", s_export.name, (unsigned long)s_export.bytes);
        alert("Report saved", s_export.name);
    } else if (s_export.err == STARK_ERR_STATE) {
        STARK_LOGI("selftest", "export skipped (no card)");
        alert("No card", "Insert a card to save the report.");
    } else {
        STARK_LOGW("selftest", "export failed: %s", stark_err_str(s_export.err));
        alert("Export failed", stark_err_str(s_export.err));
    }
}

/* ---- the menu's callbacks ----------------------------------------------------------------- */

static void activate(size_t index, void *ctx)
{
    (void)ctx;
    if (index >= s_row_count) {
        return;
    }
    const strow_t *row = &s_st->rows[index];
    if (row->kind == STROW_CHECK) {
        const stark_selftest_row_t *r = &s_st->results[row->idx];
        char msg[80];
        (void)snprintf(msg, sizeof msg, "%s%s%s", stview_result_word(r->result),
                       r->evidence[0] != '\0' ? ": " : "", r->evidence);
        alert(r->name, msg);
    } else if (s_busy != BUSY_NONE) {
        return; /* one job at a time */
    } else if (row->kind == STROW_RUN) {
        run_all();
    } else if (row->kind == STROW_EXPORT) {
        export_report();
    }
}

static bool self_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type == STARK_EVT_APP_REQUEST && e->app.id == STARK_APP_REQ_NOTIFY) {
        if (e->app.arg == NOTE_AUTOMATED_DONE) {
            s_busy = BUSY_NONE;
            show(selected());
            start_walk();
            return true;
        }
        if (e->app.arg == NOTE_EXPORT_DONE) {
            export_done();
            return true;
        }
    }
    if (e->type == STARK_EVT_SYSTEM && e->system.id == STARK_SYS_SELFTEST) {
        show(selected()); /* a result came in: the list says so */
        return true;
    }
    return s_menu_event(self, e);
}

/* Before the manager joins the worker: nothing played or walked outlives the screen. */
static void self_exit(stark_screen_t *self)
{
    (void)self;
    stark_buzzer_stop();
}

static stark_err_t self_start(void **state)
{
    s_st = calloc(1, sizeof *s_st);
    if (s_st == NULL) {
        return STARK_ERR_NO_MEM;
    }
    *state = s_st;
    s_busy = BUSY_NONE;
    s_walk_open = false;
    s_walk.step = STWALK_NONE;
    return STARK_OK;
}

static void self_stop(void *state)
{
    (void)state;
    stark_buzzer_stop();
    free(s_st);
    s_st = NULL;
}

static stark_screen_t *self_screen(void *state)
{
    (void)state;
    STARK_LOGI("selftestapp", "home");
    show(NO_SEL);
    return &s_menu.screen;
}

/* Launcher icon (16x16, 1bpp — ui_menu.h): a ticked box. */
static const uint8_t k_icon[32] = {
    0x00, 0x00, 0x7F, 0xFE, 0x40, 0x02, 0x40, 0x0A, 0x40, 0x12, 0x40, 0x22, 0x48, 0x42, 0x44, 0x82,
    0x43, 0x02, 0x41, 0x02, 0x40, 0x02, 0x40, 0x02, 0x7F, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

const stark_app_t app_selftest = {
    .id = "selftest",
    .title = "Self-Test",
    .category = "System",
    .on_start = self_start,
    .on_stop = self_stop,
    .screen = self_screen,
    .icon = k_icon,
};
