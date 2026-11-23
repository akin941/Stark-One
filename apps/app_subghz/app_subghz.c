/*
 * app_subghz.c — Sub-GHz (TASKS.md STARK-0602/0603, ADR-0022): a receive-only RSSI
 * meter at one frequency (LEFT/RIGHT step it by 25 kHz), a one-shot scan of the
 * 433.05–434.79 MHz band that finds the strongest channel, a raw capture of the data
 * line as timed symbols with its timing histogram and Save to captures/rf_NNNN.rf, and
 * the saved captures read back. The radio listens only while a view is open: BACK from
 * a view, or leaving the app, returns it to IDLE. One menu shows the home list or a view;
 * BACK from a view returns to the home list. The app never transmits (that is
 * STARK-0605, behind a confirmation).
 */
#include "stark_app.h"
#include "stark_caps.h"
#include "stark_input.h"
#include "stark_log.h"
#include "stark_storage.h"
#include "stark_subghz.h"
#include "subghzapp.h"

static sgapp_mode_t s_mode;
static ui_menu_t s_menu;
static bool (*s_menu_event)(stark_screen_t *self, const stark_event_t *e);

static bool subghz_event(stark_screen_t *self, const stark_event_t *e);
static void show(sgapp_mode_t mode);

static const char *value_of(size_t index, void *ctx)
{
    (void)ctx;
    return sgview_value(index);
}

/* The same rows with fresh text (the row count of a view never changes under it). */
static void refresh_rows(void)
{
    size_t n = 0;
    sgview_refresh();
    (void)sgview_items(s_mode, &n);
    stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
}

/* LEFT/RIGHT: the meter's frequency retunes; the capture's setup changes frequency or modulation.
 */
static bool adjust(size_t index, int dir, void *ctx)
{
    (void)ctx;
    if (s_mode == SGAPP_CAPTURE && sgview_cap_phase() == SGCAP_SETUP && index <= 1) {
        bool changed = true;
        if (index == 0) {
            changed = sgview_step_hz(dir);
        } else {
            sgview_toggle_mod();
        }
        refresh_rows();
        return changed;
    }
    if (s_mode != SGAPP_METER || index != 0 || !sgview_step_hz(dir)) {
        return false;
    }
    if (stark_subghz_meter_start(sgview_hz()) != STARK_OK) {
        sgview_refresh();
        show(SGAPP_HOME); /* the module went away under us */
        return false;
    }
    refresh_rows(); /* the new frequency, and no reading for it yet */
    return true;
}

static void capture_start(void)
{
    subghz_sym_t *buf = sgfile_buffer();
    if (buf == NULL) {
        STARK_LOGW("subghzapp", "capture refused: no memory");
        return;
    }
    if (sgfile_busy()) {
        STARK_LOGI("subghzapp", "capture refused: busy"); /* the worker holds the buffer */
        return;
    }
    stark_err_t err =
        stark_subghz_capture_start(sgview_mod(), sgview_hz(), buf, SUBGHZ_RAW_MAX_SYMBOLS);
    sgview_refresh();
    if (err != STARK_OK) {
        STARK_LOGW("subghzapp", "capture refused: %s", stark_err_str(err));
        show(SGAPP_HOME); /* the module went away under us */
        return;
    }
    sgfile_forget_save();
    show(SGAPP_CAPTURE);
}

static void capture_save(void)
{
    subghz_raw_t raw;
    if (!sgview_capture_raw(&raw, sgfile_symbols())) {
        STARK_LOGI("subghzapp", "save refused: nothing captured");
        return;
    }
    sgfile_save(&raw);
    show(SGAPP_CAPTURE); /* "saving...", the rows dimmed until the worker is done */
}

/* The finished capture, in the log: what the timing view shows. */
static void log_capture(void)
{
    stark_subghz_status_t st;
    subghz_hist_t h;
    char text[128];
    stark_subghz_status(&st);
    subghz_hist(sgfile_symbols(), sgfile_symbols() != NULL ? st.symbols : 0u, &h);
    sgview_hist_text(&h, text, sizeof text);
    STARK_LOGI("subghzapp", "capture %u sym %u us", (unsigned)st.symbols, (unsigned)st.span_us);
    STARK_LOGI("subghzapp", "hist %s", text);
}

static void activate(size_t index, void *ctx)
{
    (void)ctx;
    if (s_mode == SGAPP_HOME && sgview_is_meter_row(index)) {
        if (stark_subghz_meter_start(sgview_hz()) == STARK_OK) {
            sgview_refresh();
            show(SGAPP_METER);
        }
    } else if (s_mode == SGAPP_HOME && sgview_is_scan_row(index)) {
        if (stark_subghz_scan_start() == STARK_OK) {
            sgview_refresh();
            show(SGAPP_SCAN);
        }
    } else if (s_mode == SGAPP_HOME && sgview_is_capture_row(index)) {
        show(SGAPP_CAPTURE); /* the setup: nothing is received until Start */
    } else if (s_mode == SGAPP_HOME && sgview_is_saved_row(index)) {
        sgfile_list(); /* SGFILE_NOTE_LISTED shows it */
    } else if (s_mode == SGAPP_SCAN && sgview_is_again_row(index)) {
        if (stark_subghz_scan_start() == STARK_OK) {
            sgview_refresh();
            show(SGAPP_SCAN);
        }
    } else if (s_mode == SGAPP_CAPTURE) {
        if (sgview_is_start_row(index) || sgview_is_restart_row(index)) {
            capture_start();
        } else if (sgview_is_stop_row(index)) {
            stark_subghz_capture_finish(); /* STARK_SUBGHZ_EVT_CAPTURE_DONE follows */
        } else if (sgview_is_save_row(index)) {
            capture_save();
        }
    } else if (s_mode == SGAPP_SAVED) {
        sgfile_open(index); /* SGFILE_NOTE_OPENED shows it */
    }
}

/* Re-initialises the one menu for a view (selection starts at the top).
 * NOTE: the app manager hooks the running screen's on_exit to stop the app
 * (app_manager.c); ui_menu_init() rewrites the whole screen, so the hook is
 * carried over (as in app_nfc.c). */
static void show(sgapp_mode_t mode)
{
    void (*on_exit)(stark_screen_t *) = s_menu.screen.on_exit;
    size_t n = 0;
    const ui_menu_item_t *items = sgview_items(mode, &n);
    s_mode = sgview_ready() ? mode : SGAPP_HOME;
    ui_menu_init(&s_menu, "Sub-GHz", items, n, activate, NULL);
    s_menu.value = value_of;
    s_menu.adjust =
        s_mode == SGAPP_METER || (s_mode == SGAPP_CAPTURE && sgview_cap_phase() == SGCAP_SETUP)
            ? adjust
            : NULL;
    s_menu_event = s_menu.screen.on_event;
    s_menu.screen.on_event = subghz_event;
    s_menu.screen.on_exit = on_exit;
    stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
}

/* A worker finished (subghz_files.c): the views that wait for it. */
static void file_note(sgfile_note_t what)
{
    sgview_refresh();
    switch (what) {
        case SGFILE_NOTE_LISTED:
            show(SGAPP_SAVED);
            break;
        case SGFILE_NOTE_OPENED:
            sgview_set_file(sgfile_loaded_name(), sgfile_loaded());
            show(SGAPP_FILE);
            break;
        case SGFILE_NOTE_OPEN_FAILED:
            show(SGAPP_SAVED); /* the list again, usable now the worker is done */
            break;
        case SGFILE_NOTE_SAVED:
        case SGFILE_NOTE_SAVE_FAILED:
            if (s_mode == SGAPP_CAPTURE) {
                show(SGAPP_CAPTURE);
            }
            break;
        case SGFILE_NOTE_NONE:
        default:
            break;
    }
}

static bool subghz_event(stark_screen_t *self, const stark_event_t *e)
{
    sgfile_note_t note = SGFILE_NOTE_NONE;
    if (sgfile_event(e, &note)) {
        file_note(note);
        return true;
    }
    if (e->type == STARK_EVT_SYSTEM && e->system.id == STARK_SYS_STORAGE) {
        if ((stark_storage_state_t)e->system.arg != STARK_STORAGE_MOUNTED) {
            sgfile_forget(); /* no card: no list, and nothing can be opened */
            if (s_mode == SGAPP_SAVED) {
                sgview_refresh();
                show(SGAPP_HOME);
            }
        }
        return s_menu_event(self, e);
    }
    if (e->type == STARK_EVT_SYSTEM && e->system.id == STARK_SYS_SUBGHZ) {
        sgview_refresh();
        switch (e->system.arg) {
            case STARK_SUBGHZ_EVT_MODULE:
                show(SGAPP_HOME); /* a module came or went: the radio is idle either way */
                break;
            case STARK_SUBGHZ_EVT_RSSI:
                if (s_mode == SGAPP_METER) {
                    refresh_rows();
                }
                break;
            case STARK_SUBGHZ_EVT_SCAN_DONE:
                if (s_mode == SGAPP_SCAN) {
                    show(SGAPP_SCAN);
                }
                break;
            case STARK_SUBGHZ_EVT_CAPTURE:
                if (s_mode == SGAPP_CAPTURE && sgview_cap_phase() == SGCAP_RUNNING) {
                    refresh_rows();
                }
                break;
            case STARK_SUBGHZ_EVT_CAPTURE_DONE:
                if (s_mode == SGAPP_CAPTURE && sgview_cap_phase() == SGCAP_DONE) {
                    log_capture();
                    show(SGAPP_CAPTURE);
                }
                break;
            default:
                break;
        }
        return true;
    }
    if (s_mode != SGAPP_HOME && e->type == STARK_EVT_KEY && e->key.key == STARK_KEY_BACK &&
        e->key.action == STARK_KEY_SHORT) {
        if (s_mode == SGAPP_FILE) {
            show(SGAPP_SAVED); /* a saved capture goes back to the list */
            return true;
        }
        stark_subghz_stop();
        sgview_refresh();
        show(SGAPP_HOME); /* BACK leaves the view, not the app */
        return true;
    }
    return s_menu_event(self, e);
}

static stark_err_t subghz_start(void **state)
{
    (void)state;
    sgview_reset();
    sgview_refresh();
    return STARK_OK;
}

static void subghz_stop(void *state)
{
    (void)state;
    stark_subghz_stop(); /* the radio is never left receiving */
    sgfile_stop();       /* and the capture buffer goes back to the heap (the worker is joined) */
}

static stark_screen_t *subghz_screen(void *state)
{
    (void)state;
    show(SGAPP_HOME);
    return &s_menu.screen;
}

/* Launcher icon (16x16, 1bpp — ui_menu.h): an antenna with radiating arcs. */
static const uint8_t k_icon[32] = {
    0x00, 0x00, 0x20, 0x04, 0x44, 0x22, 0x49, 0x92, 0x4A, 0x52, 0x4A, 0x52, 0x49, 0x92, 0x44, 0x22,
    0x20, 0x04, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x03, 0xC0, 0x00, 0x00, 0x00, 0x00,
};

const stark_app_t app_subghz = {
    .id = "subghz",
    .title = "Sub-GHz",
    .category = "Lab",
    .on_start = subghz_start,
    .on_stop = subghz_stop,
    .screen = subghz_screen,
    .icon = k_icon,
    .caps_required = STARK_CAP_SUBGHZ,
};
