/*
 * app_uart.c — UART (TASKS.md STARK-0705, ADR-0023 §7): watches a serial line on the two lab pins
 * in hex and ASCII, sends bounded data, proves a round trip with a loopback test, finds the baud of
 * an incoming stream and logs what it receives to the card. 3.3 V TTL only.
 *
 * Home: Monitor, Send, Loopback test, Auto-baud, Log (on or off), Baud, Format. The tool holds the
 * pins only while a view or a job needs them, and gives them back, the driver included, when it
 * ends: BACK from a view, leaving the app, a failure. A monitor drives nothing; the first thing
 * that does (a send, the loopback test) asks a confirmation naming the TX pin ("own hardware
 * only"), and OK arms it for the rest of that view. The monitor is a screen of its own: a header
 * (baud, format, counters, the log's state) and the last bytes of a 4 kB ring, hex (16 per row) or
 * ASCII (48), OK toggles, UP and DOWN scroll the ring, LEFT and RIGHT a page, BACK closes the
 * driver. It polls on the app worker (10 ms waits, bounded buffers, every loss counted and shown)
 * and the card log is written there too. The loopback test, a send from the file and the auto-baud
 * capture wait on the worker as well and BACK cuts them short. The row logic is uart_views.c
 * (pure); this file binds it to the service and the screens.
 */
#include <stdio.h>
#include <string.h>
#include "gfx_font.h"
#include "stark_app.h"
#include "stark_event.h"
#include "stark_hal.h"
#include "stark_input.h"
#include "stark_lab.h"
#include "stark_log.h"
#include "stark_storage.h"
#include "stark_theme.h"
#include "ui_dialog.h"
#include "ui_menu.h"
#include "uartapp.h"

#define NOTE_MONITOR_ENDED 400 /* STARK_APP_REQ_NOTIFY arg: the worker's monitor has ended */
#define NOTE_LOOP_DONE     401 /* ... the loopback test has ended */
#define NOTE_AUTOBAUD_DONE 402 /* ... the auto-baud capture has ended */
#define NOTE_FILE_DONE     403 /* ... the send from the file has ended */

#define POLL_MS            10u
#define TX_PIN_NAME        "LAB_UART_TX"
#define MONO10_PITCH       10
#define HEAD_H             16 /* two lines of the 8x16 font, then one of the 6x10 */

typedef enum { VIEW_HOME, VIEW_SEND, VIEW_LOOP } view_t;
typedef enum { BUSY_NONE, BUSY_MONITOR, BUSY_LOOP, BUSY_AUTOBAUD, BUSY_FILE } busy_t;
typedef enum { PEND_NONE, PEND_BYTES, PEND_FILE, PEND_LOOP } pend_t;
enum { LOOP_RUN, LOOP_BAUD, LOOP_FORMAT, LOOP_LINE, LOOP_ROWS };

static view_t s_view;
static ui_menu_t s_menu;
static bool (*s_menu_event)(stark_screen_t *self, const stark_event_t *e);
static ui_menu_item_t s_items[UARTAPP_ROWS_MAX];
static size_t s_item_count;

static size_t s_baud; /* indices into the lists */
static size_t s_fmt;
static bool s_log_on;
static bool s_hex;
static char s_loop_text[32];
static char s_ab_text[24];
static stark_lab_loop_t s_loop; /* the last result, for the line row */
static bool s_have_loop;

static uartapp_send_cfg_t s_cfg;
static uartrow_t s_rows[UARTAPP_ROWS_MAX];
static size_t s_row_count;
static char s_status[24];
static uartrow_t s_status_row;

static stark_screen_t s_mon; /* the monitor: pushed over the menu */
static bool s_mon_open;      /* it is on the stack */
static size_t s_scroll;      /* rows back from the newest */

static busy_t s_busy;
static bool s_leaving; /* BACK was pressed while a job ran: go home when it ends */
static volatile bool s_cancel;
static pend_t s_pend;
static struct {
    stark_err_t err;
    stark_lab_loop_t loop;
    stark_lab_autobaud_t ab;
    uint8_t data[UARTAPP_SEND_MAX];
    size_t n;
    char text[24];
} s_job;

static void show(view_t view);
static bool uart_event(stark_screen_t *self, const stark_event_t *e);
static void uart_exit(stark_screen_t *self);
static void mon_exit(stark_screen_t *self);

static uint32_t now_ms(void)
{
    return (uint32_t)(stark_hal_now_us() / 1000u);
}

static void notify_done(int note)
{
    stark_event_t e = {.type = STARK_EVT_APP_REQUEST};
    e.app.id = STARK_APP_REQ_NOTIFY;
    e.app.arg = note;
    (void)stark_event_publish(&e);
}

static void alert(const char *title, const char *what, stark_err_t err)
{
    char msg[48];
    (void)snprintf(msg, sizeof msg, "%s: %s.", what, uartview_error_text(err));
    (void)stark_ui_dialog_alert(title, msg, NULL, NULL);
}

static bool session_open(void)
{
    stark_lab_uart_stats_t st;
    stark_lab_uart_stats(&st);
    return st.open;
}

static bool tx_on(void)
{
    stark_lab_uart_stats_t st;
    stark_lab_uart_stats(&st);
    return st.tx;
}

/* Opens the session at the chosen baud and format, or says why not. */
static bool open_session(void)
{
    stark_err_t err = stark_lab_uart_open(s_baud, s_fmt);
    if (err != STARK_OK) {
        STARK_LOGW("uartapp", "not opened: %s", stark_err_str(err));
        alert("UART", "Not opened", err);
        return false;
    }
    return true;
}

static size_t baud_index_of(uint32_t baud)
{
    for (size_t i = 0; i < STARK_LAB_UART_BAUDS; i++) {
        if (stark_lab_uart_baud(i) == baud) {
            return i;
        }
    }
    return s_baud;
}

/* ---- rows of each view ------------------------------------------------------------------ */

static void fill_home(void)
{
    for (size_t i = 0; i < UARTHOME_ROWS; i++) {
        s_items[i] = (ui_menu_item_t){.label = uartview_home_label(i)};
    }
    s_item_count = UARTHOME_ROWS;
}

static void fill_send(void)
{
    s_row_count = uartview_send_rows(&s_cfg, s_rows);
    for (size_t i = 0; i < s_row_count; i++) {
        s_items[i] = (ui_menu_item_t){.label = uartview_send_label(&s_rows[i])};
    }
    s_item_count = s_row_count;
}

static void fill_loop(void)
{
    s_items[LOOP_RUN] = (ui_menu_item_t){.label = "Run test"};
    s_items[LOOP_BAUD] = (ui_menu_item_t){.label = "Baud"};
    s_items[LOOP_FORMAT] = (ui_menu_item_t){.label = "Format"};
    s_items[LOOP_LINE] = (ui_menu_item_t){.label = "Line errors", .disabled = true};
    s_item_count = LOOP_ROWS;
}

static const char *value_of(size_t index, void *ctx)
{
    static char buf[32];
    (void)ctx;
    switch (s_view) {
        case VIEW_HOME:
            if (index == UARTHOME_AUTOBAUD && s_busy == BUSY_AUTOBAUD) {
                return "listening...";
            }
            return uartview_home_value(index, s_log_on, s_baud, s_fmt, s_loop_text, s_ab_text, buf,
                                       sizeof buf);
        case VIEW_LOOP:
            if (index == LOOP_RUN) {
                return s_busy == BUSY_LOOP ? "running..."
                                           : (s_loop_text[0] != '\0' ? s_loop_text : NULL);
            }
            if (index == LOOP_LINE) {
                (void)snprintf(buf, sizeof buf, "%u %u %u", (unsigned)s_loop.framing,
                               (unsigned)s_loop.parity, (unsigned)s_loop.overrun);
                return s_have_loop ? buf : NULL;
            }
            return uartview_home_value(index == LOOP_BAUD ? UARTHOME_BAUD : UARTHOME_FORMAT, false,
                                       s_baud, s_fmt, "", "", buf, sizeof buf);
        case VIEW_SEND:
            if (index < s_row_count) {
                bool status_here = s_rows[index].kind == s_status_row.kind &&
                                   s_rows[index].idx == s_status_row.idx;
                return uartview_send_value(&s_rows[index], &s_cfg, status_here ? s_status : NULL,
                                           buf, sizeof buf);
            }
            return NULL;
        default:
            return NULL;
    }
}

static void activate(size_t index, void *ctx);
static bool adjust(size_t index, int dir, void *ctx);

/*
 * Re-initialises the one menu for a view. NOTE: the app manager hooks the running screen's on_exit
 * to stop the app (app_manager.c); ui_menu_init() rewrites the whole screen, so the hook is carried
 * over, and the first call installs uart_exit() as the screen's own, which the manager keeps.
 */
static void show(view_t view)
{
    void (*on_exit)(stark_screen_t *) =
        s_menu.screen.on_exit != NULL ? s_menu.screen.on_exit : uart_exit;
    s_view = view;
    switch (view) {
        case VIEW_SEND:
            fill_send();
            break;
        case VIEW_LOOP:
            fill_loop();
            break;
        case VIEW_HOME:
        default:
            fill_home();
            break;
    }
    /* NOTE: the title stays "UART": the status bar repaints it only when a screen is pushed. */
    ui_menu_init(&s_menu, "UART", s_items, s_item_count, activate, NULL);
    s_menu.adjust = (view == VIEW_SEND || view == VIEW_HOME || view == VIEW_LOOP) ? adjust : NULL;
    s_menu.value = value_of;
    s_menu_event = s_menu.screen.on_event;
    s_menu.screen.on_event = uart_event;
    s_menu.screen.on_exit = on_exit;
    stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
}

/* The rows of the Send view changed (the byte count moved): rebuild them, keep the selection. */
static void rebuild_send(void)
{
    size_t sel =
        ui_menu_model_has_selection(&s_menu.model) ? ui_menu_model_selected(&s_menu.model) : 0u;
    show(VIEW_SEND);
    for (size_t guard = 0; guard < s_item_count && ui_menu_model_has_selection(&s_menu.model) &&
                           ui_menu_model_selected(&s_menu.model) != sel;
         guard++) {
        (void)ui_menu_model_move(&s_menu.model, 1);
    }
}

static void repaint(void)
{
    stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
}

/* Back at home: the session is closed and the pins are free. */
static void go_home(void)
{
    stark_lab_uart_close();
    s_pend = PEND_NONE;
    s_leaving = false;
    show(VIEW_HOME);
}

/* ---- the transmit confirmation --------------------------------------------------------------- */

static void do_pending(void);

static void tx_confirmed(stark_dialog_result_t result, void *ctx)
{
    (void)ctx;
    if (result != STARK_DIALOG_OK) {
        STARK_LOGI("uartapp", "tx cancelled");
        s_pend = PEND_NONE;
        return;
    }
    stark_err_t err = stark_lab_uart_tx_enable();
    if (err != STARK_OK) {
        STARK_LOGW("uartapp", "tx not enabled: %s", stark_err_str(err));
        s_pend = PEND_NONE;
        alert("UART", "TX not enabled", err);
        go_home();
        return;
    }
    STARK_LOGI("uartapp", "tx on");
    do_pending();
}

/* The first drive of the TX pin in this view: asks, and goes on with `what` when OK. */
static void request_tx(pend_t what)
{
    if (tx_on()) {
        s_pend = what;
        do_pending();
        return;
    }
    char msg[160];
    uartview_tx_message(TX_PIN_NAME, msg, sizeof msg);
    STARK_LOGI("uartapp", "confirm tx");
    s_pend = what;
    if (stark_ui_dialog_confirm("Drive " TX_PIN_NAME "?", msg, tx_confirmed, NULL) != STARK_OK) {
        s_pend = PEND_NONE;
    }
}

/* ---- workers -----------------------------------------------------------------------------------
 */

static void monitor_worker(void *ctx)
{
    (void)ctx;
    uint8_t chunk[STARK_LAB_UART_CHUNK];
    s_job.err = STARK_OK;
    if (s_log_on) {
        (void)
            uartlog_start(); /* a refusal is a log line and the log's state; the monitor goes on */
    }
    while (!stark_app_worker_should_stop() && !s_cancel) {
        size_t got = 0;
        stark_err_t err = stark_lab_uart_poll(chunk, sizeof chunk, POLL_MS, &got);
        if (err != STARK_OK) {
            /* STATE is the screen closing the session under the worker: not a failure */
            s_job.err = s_cancel || err == STARK_ERR_STATE ? STARK_OK : err;
            break;
        }
        if (got > 0u) {
            uartlog_write(chunk, got, now_ms());
        }
        uartlog_tick(now_ms());
    }
    uartlog_stop();
    notify_done(NOTE_MONITOR_ENDED);
}

static void loop_worker(void *ctx)
{
    (void)ctx;
    s_job.err = stark_lab_uart_loop_test(&s_job.loop);
    notify_done(NOTE_LOOP_DONE);
}

static void autobaud_worker(void *ctx)
{
    (void)ctx;
    s_job.err = stark_lab_uart_autobaud(&s_job.ab);
    notify_done(NOTE_AUTOBAUD_DONE);
}

/* The send file: read, parsed strictly (rejected whole), sent once. */
static void file_worker(void *ctx)
{
    (void)ctx;
    uint8_t text[STARK_LAB_UART_FILE_MAX + 1u];
    size_t got = 0;
    stark_file_t *f = NULL;
    stark_err_t err = stark_storage_open(STARK_DIR_APPS, UARTAPP_SEND_FILE, STARK_FILE_READ, &f);
    s_job.err = STARK_OK;
    if (err == STARK_OK) {
        err = stark_storage_read(f, text, sizeof text, &got);
        (void)stark_storage_close(f);
    }
    if (err != STARK_OK) {
        (void)snprintf(s_job.text, sizeof s_job.text, "%s",
                       err == STARK_ERR_NOT_FOUND ? "no file" : "file error");
    } else {
        size_t count = 0;
        stark_lab_file_t why = stark_lab_uart_parse_send(text, got, s_job.data, &count);
        if (why != STARK_LAB_FILE_OK) {
            (void)snprintf(s_job.text, sizeof s_job.text, "rejected: %s",
                           stark_lab_uart_file_text(why));
        } else {
            s_job.err = stark_lab_uart_send(s_job.data, count);
            (void)snprintf(s_job.text, sizeof s_job.text, "%s",
                           s_job.err == STARK_OK ? "sent" : "send failed");
            s_job.n = count;
        }
    }
    notify_done(NOTE_FILE_DONE);
}

static bool start_job(busy_t what, stark_app_worker_fn fn)
{
    s_busy = what;
    stark_err_t err = stark_app_worker_start(fn, NULL);
    if (err != STARK_OK) {
        s_busy = BUSY_NONE;
        STARK_LOGW("uartapp", "worker refused: %s", stark_err_str(err));
        return false;
    }
    return true;
}

/* ---- actions -----------------------------------------------------------------------------------
 */

static void send_bytes(void)
{
    stark_err_t err = stark_lab_uart_send(s_job.data, s_job.n);
    if (err == STARK_OK) {
        (void)snprintf(s_status, sizeof s_status, "sent %u", (unsigned)s_job.n);
        STARK_LOGI("uartapp", "sent %u bytes", (unsigned)s_job.n);
    } else {
        (void)snprintf(s_status, sizeof s_status, "%s", uartview_error_text(err));
        STARK_LOGW("uartapp", "send failed: %s", stark_err_str(err));
        if (!session_open()) {
            alert("UART", "Send failed", err);
            go_home();
            return;
        }
    }
    repaint();
}

/* Goes on with the action the confirmation was for (or that needed none). */
static void do_pending(void)
{
    pend_t what = s_pend;
    s_pend = PEND_NONE;
    switch (what) {
        case PEND_BYTES:
            send_bytes();
            break;
        case PEND_FILE:
            s_status[0] = '\0';
            if (start_job(BUSY_FILE, file_worker)) {
                (void)snprintf(s_status, sizeof s_status, "sending...");
                repaint();
            }
            break;
        case PEND_LOOP:
            if (start_job(BUSY_LOOP, loop_worker)) {
                (void)snprintf(s_loop_text, sizeof s_loop_text, "running...");
                repaint();
            }
            break;
        case PEND_NONE:
        default:
            break;
    }
}

static void open_send(void)
{
    if (!open_session()) {
        return;
    }
    uartview_send_cfg_reset(&s_cfg);
    s_status[0] = '\0';
    s_status_row = (uartrow_t){UARTROW_SEND, 0};
    STARK_LOGI("uartapp", "send view");
    show(VIEW_SEND);
}

static void open_loop(void)
{
    if (!open_session()) {
        return;
    }
    STARK_LOGI("uartapp", "loop view");
    show(VIEW_LOOP);
}

static void start_autobaud(void)
{
    s_ab_text[0] = '\0';
    if (start_job(BUSY_AUTOBAUD, autobaud_worker)) {
        STARK_LOGI("uartapp", "autobaud listening");
        repaint();
    }
}

static void toggle_log(void)
{
    if (!s_log_on) {
        stark_storage_state_t card = stark_storage_state();
        if (card != STARK_STORAGE_MOUNTED) {
            STARK_LOGI("uartapp", "log refused: %s", stark_storage_state_str(card));
            (void)stark_ui_dialog_alert("Log", "No card: nothing to log to.", NULL, NULL);
            return;
        }
    }
    s_log_on = !s_log_on;
    STARK_LOGI("uartapp", "log %s", s_log_on ? "on" : "off");
    repaint();
}

/* A new baud or format: a session that is open follows (and loses its ring); the home list only
 * remembers it. */
static void apply_format(void)
{
    STARK_LOGI("uartapp", "%u %s", (unsigned)stark_lab_uart_baud(s_baud),
               stark_lab_uart_fmt_name(s_fmt));
    if (!session_open()) {
        repaint();
        return;
    }
    stark_err_t err = stark_lab_uart_set_format(s_baud, s_fmt);
    if (err != STARK_OK) {
        STARK_LOGW("uartapp", "not changed: %s", stark_err_str(err));
        alert("UART", "Not changed", err);
        go_home();
        return;
    }
    s_loop_text[0] = '\0'; /* a result belongs to the settings it was made with */
    s_have_loop = false;
    repaint();
}

/* ---- the monitor --------------------------------------------------------------------------------
 */

static void mon_render(stark_screen_t *self, gfx_surface_t *s)
{
    (void)self;
    gfx_rect_t c = stark_ui_content_rect();
    stark_lab_uart_stats_t st;
    char line[64];
    gfx_fill(s, s->clip, STARK_THEME_BG);
    stark_lab_uart_stats(&st);
    uartview_settings_text(st.baud_index, st.fmt_index, line, sizeof line);
    int16_t x = STARK_THEME_TEXT_X;
    int16_t w =
        gfx_text(s, &gfx_font_mono16, x, c.y, line, STARK_THEME_ACTIVE, STARK_THEME_BG, true);
    (void)gfx_text(s, &gfx_font_mono16, (int16_t)(x + w + 16), c.y, s_hex ? "HEX" : "ASCII",
                   STARK_THEME_FG, STARK_THEME_BG, true);
    uartview_stats_text(&st, line, sizeof line);
    bool errors = st.framing + st.parity + st.overrun > 0u || st.lost > 0u;
    (void)gfx_text(s, &gfx_font_mono16, x, (int16_t)(c.y + HEAD_H), line,
                   errors ? STARK_THEME_ERROR : STARK_THEME_FG, STARK_THEME_BG, true);
    uartview_status_text(uartlog_state(), uartlog_name(), uartlog_bytes(), s_scroll, line,
                         sizeof line);
    (void)gfx_text(s, &gfx_font_mono10, x, (int16_t)(c.y + 2 * HEAD_H), line, STARK_THEME_DISABLED,
                   STARK_THEME_BG, true);
    size_t first = 0;
    size_t shown = uartview_window(s_hex, st.buffered, s_scroll, UARTAPP_VISIBLE, &first);
    for (size_t i = 0; i < shown; i++) {
        size_t skip = 0;
        size_t len = 0;
        uint8_t bytes[48];
        char text[64];
        uartview_row_span(s_hex, st.buffered, first + i, &skip, &len);
        len = stark_lab_uart_ring_read(skip, bytes, len < sizeof bytes ? len : sizeof bytes);
        uartview_row_text(s_hex, bytes, len, text, sizeof text);
        (void)gfx_text(s, &gfx_font_mono10, x,
                       (int16_t)(c.y + 2 * HEAD_H + 14 + (int)i * MONO10_PITCH), text,
                       STARK_THEME_FG, STARK_THEME_BG, true);
    }
}

static size_t mon_max_scroll(void)
{
    stark_lab_uart_stats_t st;
    stark_lab_uart_stats(&st);
    return uartview_max_scroll(s_hex, st.buffered, UARTAPP_VISIBLE);
}

static void handle_note(int note);

static bool mon_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type == STARK_EVT_APP_REQUEST && e->app.id == STARK_APP_REQ_NOTIFY) {
        handle_note(e->app.arg);
        return true;
    }
    if (e->type == STARK_EVT_SYSTEM && e->system.id == STARK_SYS_LAB &&
        (e->system.arg == STARK_LAB_EVT_UART_DATA || e->system.arg == STARK_LAB_EVT_UART_ERROR)) {
        stark_ui_invalidate(self, stark_ui_content_rect());
        return true;
    }
    if (e->type != STARK_EVT_KEY) {
        return false;
    }
    bool press_or_repeat = e->key.action == STARK_KEY_PRESS || e->key.action == STARK_KEY_REPEAT;
    int delta = 0;
    switch (e->key.key) {
        case STARK_KEY_UP:
            delta = 1;
            break;
        case STARK_KEY_DOWN:
            delta = -1;
            break;
        case STARK_KEY_LEFT:
            delta = (int)UARTAPP_VISIBLE - 1;
            break;
        case STARK_KEY_RIGHT:
            delta = -((int)UARTAPP_VISIBLE - 1);
            break;
        case STARK_KEY_OK:
            if (e->key.action == STARK_KEY_SHORT) {
                s_hex = !s_hex;
                s_scroll = 0;
                STARK_LOGI("uartapp", "view %s", s_hex ? "hex" : "ascii");
                stark_ui_invalidate(self, stark_ui_content_rect());
            }
            return true;
        case STARK_KEY_BACK: /* left to stark_ui: it pops the monitor, which closes the session */
        case STARK_KEY_COUNT:
        default:
            return false;
    }
    if (press_or_repeat) {
        size_t now = uartview_scroll(s_scroll, delta, mon_max_scroll());
        if (now != s_scroll) {
            s_scroll = now;
            stark_ui_invalidate(self, stark_ui_content_rect());
        }
    }
    return true;
}

/* The monitor leaves the stack (BACK, or the app stopping): the worker is told, and the session is
 * closed under it, so the pins are free the moment the screen is gone. */
static void mon_exit(stark_screen_t *self)
{
    (void)self;
    s_mon_open = false;
    s_cancel = true;
    stark_lab_uart_close();
    STARK_LOGI("uartapp", "monitor closed");
}

static void start_monitor(void)
{
    if (!open_session()) {
        return;
    }
    s_cancel = false;
    s_scroll = 0;
    stark_lab_uart_clear();
    if (!start_job(BUSY_MONITOR, monitor_worker)) {
        stark_lab_uart_close();
        return;
    }
    s_mon = (stark_screen_t){
        .name = "UART", .on_event = mon_event, .on_exit = mon_exit, .on_render = mon_render};
    if (stark_ui_push(&s_mon) != STARK_OK) {
        s_cancel = true;
        stark_lab_uart_close();
        return;
    }
    s_mon_open = true;
    STARK_LOGI("uartapp", "monitor %u %s", (unsigned)stark_lab_uart_baud(s_baud),
               stark_lab_uart_fmt_name(s_fmt));
}

/* ---- the worker's results -----------------------------------------------------------------------
 */

static void handle_note(int note)
{
    switch (note) {
        case NOTE_MONITOR_ENDED:
            s_busy = BUSY_NONE;
            if (s_job.err != STARK_OK) {
                STARK_LOGW("uartapp", "monitor failed: %s", stark_err_str(s_job.err));
                if (s_mon_open) {
                    (void)stark_ui_pop(); /* the monitor, which closes the session */
                    alert("UART", "Monitor stopped", s_job.err);
                }
            }
            break;
        case NOTE_LOOP_DONE:
            s_busy = BUSY_NONE;
            if (s_leaving || s_view != VIEW_LOOP) {
                go_home();
                break;
            }
            if (s_job.err == STARK_OK) {
                s_loop = s_job.loop;
                s_have_loop = true;
                stark_lab_loop_text(&s_loop, s_loop_text, sizeof s_loop_text);
                STARK_LOGI("uartapp", "loop %s", s_loop_text);
            } else {
                s_loop_text[0] = '\0';
                STARK_LOGW("uartapp", "loop not run: %s", stark_err_str(s_job.err));
                if (!session_open()) {
                    alert("UART", "Test failed", s_job.err);
                    go_home();
                    break;
                }
            }
            repaint();
            break;
        case NOTE_AUTOBAUD_DONE:
            s_busy = BUSY_NONE;
            if (s_job.err == STARK_OK) {
                char text[16];
                (void)snprintf(s_ab_text, sizeof s_ab_text, "%s",
                               stark_lab_autobaud_text(&s_job.ab, text, sizeof text));
                STARK_LOGI("uartapp", "autobaud %s", s_ab_text);
                if (s_job.ab.status == STARK_LAB_BAUD_OK) {
                    s_baud = baud_index_of(s_job.ab.baud);
                }
            } else {
                s_ab_text[0] = '\0';
                STARK_LOGW("uartapp", "autobaud not run: %s", stark_err_str(s_job.err));
            }
            repaint();
            break;
        case NOTE_FILE_DONE:
            s_busy = BUSY_NONE;
            if (s_leaving) {
                go_home();
                break;
            }
            if (s_job.err == STARK_OK && strcmp(s_job.text, "sent") == 0) {
                (void)snprintf(s_status, sizeof s_status, "sent %u", (unsigned)s_job.n);
                STARK_LOGI("uartapp", "file sent %u bytes", (unsigned)s_job.n);
            } else {
                (void)snprintf(s_status, sizeof s_status, "%s", s_job.text);
                STARK_LOGW("uartapp", "file %s", s_job.text);
                if (!session_open()) {
                    alert("UART", "Send failed", s_job.err);
                    go_home();
                    break;
                }
            }
            s_status_row = (uartrow_t){UARTROW_FILE, 0};
            repaint();
            break;
        default:
            break;
    }
}

/* ---- the menu's callbacks -----------------------------------------------------------------------
 */

static void activate(size_t index, void *ctx)
{
    (void)ctx;
    if (s_busy != BUSY_NONE) {
        return;
    }
    switch (s_view) {
        case VIEW_HOME:
            switch ((uarthome_row_t)index) {
                case UARTHOME_MONITOR:
                    start_monitor();
                    break;
                case UARTHOME_SEND:
                    open_send();
                    break;
                case UARTHOME_LOOP:
                    open_loop();
                    break;
                case UARTHOME_AUTOBAUD:
                    start_autobaud();
                    break;
                case UARTHOME_LOG:
                    toggle_log();
                    break;
                case UARTHOME_BAUD:
                case UARTHOME_FORMAT:
                case UARTHOME_ROWS:
                default:
                    break;
            }
            break;
        case VIEW_LOOP:
            if (index == LOOP_RUN) {
                request_tx(PEND_LOOP);
            }
            break;
        case VIEW_SEND:
            if (index >= s_row_count) {
                break;
            }
            s_status_row = s_rows[index];
            s_status[0] = '\0';
            if (s_rows[index].kind == UARTROW_CANNED) {
                const uint8_t *bytes = uartview_canned(s_rows[index].idx, &s_job.n);
                memcpy(s_job.data, bytes, s_job.n);
                request_tx(PEND_BYTES);
            } else if (s_rows[index].kind == UARTROW_FILE) {
                request_tx(PEND_FILE);
            } else if (s_rows[index].kind == UARTROW_SEND) {
                memcpy(s_job.data, s_cfg.data, s_cfg.count);
                s_job.n = s_cfg.count;
                request_tx(PEND_BYTES);
            }
            break;
        default:
            break;
    }
}

/* LEFT/RIGHT: the baud and format at home and in the loopback view, the editor's values in Send. */
static bool adjust(size_t index, int dir, void *ctx)
{
    (void)ctx;
    if (s_busy != BUSY_NONE) {
        return false;
    }
    if (s_view == VIEW_SEND) {
        if (index >= s_row_count || !uartview_send_adjust(&s_cfg, &s_rows[index], dir)) {
            return false;
        }
        if (s_rows[index].kind == UARTROW_COUNT) {
            rebuild_send();
        } else {
            repaint();
        }
        return true;
    }
    bool baud_row = s_view == VIEW_HOME ? index == UARTHOME_BAUD : index == LOOP_BAUD;
    bool fmt_row = s_view == VIEW_HOME ? index == UARTHOME_FORMAT : index == LOOP_FORMAT;
    if (baud_row && uartview_step(&s_baud, STARK_LAB_UART_BAUDS, dir)) {
        apply_format();
        return true;
    }
    if (fmt_row && uartview_step(&s_fmt, STARK_LAB_UART_FMTS, dir)) {
        apply_format();
        return true;
    }
    return false;
}

static bool uart_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type == STARK_EVT_APP_REQUEST && e->app.id == STARK_APP_REQ_NOTIFY) {
        handle_note(e->app.arg);
        return true;
    }
    if (e->type == STARK_EVT_KEY && s_busy != BUSY_NONE && s_busy != BUSY_MONITOR) {
        /* a job is waiting on the worker: BACK cuts it short, no other key does anything */
        if (e->key.key == STARK_KEY_BACK && e->key.action == STARK_KEY_SHORT) {
            s_leaving = s_view != VIEW_HOME;
            stark_lab_uart_cancel();
        }
        return true;
    }
    if (e->type == STARK_EVT_KEY && e->key.key == STARK_KEY_BACK &&
        e->key.action == STARK_KEY_SHORT && s_view != VIEW_HOME) {
        go_home(); /* BACK leaves the view, not the app; the pins and the driver go with it */
        return true;
    }
    return s_menu_event(self, e);
}

/* Before the manager joins the worker: whatever it waits on ends now. */
static void uart_exit(stark_screen_t *self)
{
    (void)self;
    s_cancel = true;
    stark_lab_uart_cancel();
}

static stark_err_t uart_start(void **state)
{
    (void)state;
    s_view = VIEW_HOME;
    s_busy = BUSY_NONE;
    s_leaving = false;
    s_cancel = false;
    s_pend = PEND_NONE;
    s_baud = 7; /* 115 200 */
    s_fmt = 0;  /* 8N1 */
    s_log_on = false;
    s_hex = true;
    s_loop_text[0] = '\0';
    s_ab_text[0] = '\0';
    s_have_loop = false;
    s_scroll = 0;
    s_mon_open = false;
    return STARK_OK;
}

/* The worker is joined already: nothing may stay open, held or armed. */
static void uart_stop(void *state)
{
    (void)state;
    s_busy = BUSY_NONE;
    stark_lab_uart_close();
    uartlog_stop();
}

static stark_screen_t *uart_screen(void *state)
{
    (void)state;
    STARK_LOGI("uartapp", "home");
    show(VIEW_HOME);
    return &s_menu.screen;
}

/* Launcher icon (16x16, 1bpp — ui_menu.h): a line with two arrows. */
static const uint8_t k_icon[32] = {
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x0C, 0x00, 0x7E, 0x00, 0x0C, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x10, 0x00, 0x30, 0x00, 0x7E, 0x00, 0x30, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,
};

const stark_app_t app_uart = {
    .id = "uart",
    .title = "UART",
    .category = "Lab",
    .on_start = uart_start,
    .on_stop = uart_stop,
    .screen = uart_screen,
    .icon = k_icon,
    .caps_required = 0,
};
