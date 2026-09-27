/*
 * ui_dialog.c — the modal dialog (ui_dialog.h): one static instance, an
 * overlay screen on the stack. Geometry is ui_dialog_layout.c (pure).
 */
#include "ui_dialog.h"
#include <stdbool.h>
#include "gfx_font.h"
#include "stark_input.h"
#include "stark_log.h"
#include "stark_theme.h"
#include "stark_ui.h"
#include "ui_dialog_layout.h"

#define TITLE_FONT (&gfx_font_mono16)
#define BODY_FONT  (&gfx_font_mono16)
#define HINT_FONT  (&gfx_font_mono10)

static struct {
    bool open;
    bool closing; /* closed by a key: `result` holds the answer */
    bool alert;
    bool ok_armed;   /* OK was pressed while open */
    bool back_armed; /* BACK was pressed while open */
    stark_dialog_result_t result;
    stark_dialog_done_fn done;
    void *ctx;
    char title[UI_DIALOG_TITLE_MAX];
    char message[UI_DIALOG_MSG_MAX];
    ui_dialog_layout_t lay;
    stark_screen_t screen;
} s_dlg;

static void finish(stark_dialog_result_t result)
{
    s_dlg.result = result;
    s_dlg.closing = true;
    (void)stark_ui_pop(); /* -> dialog_exit() */
}

static bool dialog_event(stark_screen_t *self, const stark_event_t *e)
{
    (void)self;
    if (e->type != STARK_EVT_KEY) {
        return false; /* modal: the screen beneath waits */
    }
    if (e->key.key == STARK_KEY_OK) {
        if (e->key.action == STARK_KEY_PRESS) {
            s_dlg.ok_armed = true;
        } else if (e->key.action == STARK_KEY_SHORT && s_dlg.ok_armed) {
            finish(STARK_DIALOG_OK);
        }
    } else if (e->key.key == STARK_KEY_BACK) {
        if (e->key.action == STARK_KEY_PRESS) {
            s_dlg.back_armed = true;
        } else if (e->key.action == STARK_KEY_SHORT && s_dlg.back_armed) {
            finish(s_dlg.alert ? STARK_DIALOG_OK : STARK_DIALOG_CANCEL);
        }
    }
    return true; /* every key is the dialog's while it is open */
}

static void dialog_exit(stark_screen_t *self)
{
    (void)self;
    stark_dialog_result_t result = s_dlg.closing ? s_dlg.result : STARK_DIALOG_CANCEL;
    stark_dialog_done_fn done = s_dlg.done;
    void *ctx = s_dlg.ctx;
    s_dlg.open = false; /* free before `done`, which may open another */
    s_dlg.closing = false;
    STARK_LOGI("dialog", "%s", result == STARK_DIALOG_OK ? "ok" : "cancel");
    if (done != NULL) {
        done(result, ctx);
    }
}

static void dialog_render(stark_screen_t *self, gfx_surface_t *s)
{
    (void)self;
    const ui_dialog_layout_t *l = &s_dlg.lay;
    gfx_fill(s, l->box, STARK_THEME_BG);
    for (int16_t i = 0; i < UI_DIALOG_BORDER; i++) {
        gfx_rect(s,
                 (gfx_rect_t){(int16_t)(l->box.x + i), (int16_t)(l->box.y + i),
                              (int16_t)(l->box.w - 2 * i), (int16_t)(l->box.h - 2 * i)},
                 STARK_THEME_ACCENT);
    }
    char title[UI_DIALOG_TITLE_MAX];
    (void)ui_utf8_copy(title, l->title_bytes + 1 < sizeof title ? l->title_bytes + 1 : sizeof title,
                       s_dlg.title);
    (void)gfx_text(s, TITLE_FONT, l->title.x, l->title.y, title, STARK_THEME_FG, STARK_THEME_BG,
                   true);
    if (l->lines > 0) {
        (void)gfx_text_box(s, BODY_FONT, l->body, 0, s_dlg.message, STARK_THEME_FG, STARK_THEME_BG,
                           true);
    }
    (void)gfx_text(s, HINT_FONT, l->hint.x, l->hint.y,
                   s_dlg.alert ? "OK: close" : "OK: confirm  BACK: cancel", STARK_THEME_DISABLED,
                   STARK_THEME_BG, true);
}

static stark_err_t open_dialog(bool alert, const char *title, const char *message,
                               stark_dialog_done_fn done, void *ctx)
{
    if (title == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_dlg.open) {
        return STARK_ERR_BUSY;
    }
    s_dlg.alert = alert;
    s_dlg.closing = false;
    s_dlg.ok_armed = false;
    s_dlg.back_armed = false;
    s_dlg.done = done;
    s_dlg.ctx = ctx;
    (void)ui_utf8_copy(s_dlg.title, sizeof s_dlg.title, title);
    (void)ui_utf8_copy(s_dlg.message, sizeof s_dlg.message, message);
    ui_dialog_layout(TITLE_FONT, BODY_FONT, HINT_FONT, s_dlg.title, s_dlg.message,
                     stark_ui_content_rect(), &s_dlg.lay);
    s_dlg.screen = (stark_screen_t){.name = s_dlg.title,
                                    .on_event = dialog_event,
                                    .on_exit = dialog_exit,
                                    .on_render = dialog_render,
                                    .overlay = true};
    s_dlg.open = true;
    stark_err_t err = stark_ui_push(&s_dlg.screen);
    if (err != STARK_OK) {
        s_dlg.open = false;
        return err;
    }
    STARK_LOGI("dialog", "open \"%s\"", s_dlg.title);
    return STARK_OK;
}

stark_err_t stark_ui_dialog_confirm(const char *title, const char *message,
                                    stark_dialog_done_fn done, void *ctx)
{
    return open_dialog(false, title, message, done, ctx);
}

stark_err_t stark_ui_dialog_alert(const char *title, const char *message, stark_dialog_done_fn done,
                                  void *ctx)
{
    return open_dialog(true, title, message, done, ctx);
}
