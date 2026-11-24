/*
 * app_i2c.c — I2C (TASKS.md STARK-0704, ADR-0023 §5): scans the shared I2C bus (address-only, 0x08
 * to 0x77), names likely devices from a small generic table ("may be ...": a lookup by address,
 * nothing is probed to confirm it), reads and dumps registers, and writes them only after a
 * confirmation. Home: Scan and Speed (100 or 400 kHz). Scan lists what answered, or `No devices`
 * or `Bus error`; OK on a device opens Read, Dump and Write. The editors are rows: the register,
 * its width, which nibble LEFT and RIGHT change, a length (a read) or a byte count and the bytes (a
 * write). A read can change a device's state and says so; a dump (256 bytes) and a write ask a
 * confirmation that names the address, the register and the bytes ("Own hardware only"), and OK
 * arms exactly that one call. A module's ID EEPROM and the NFC reader (while NFC is confirmed) are
 * refused by the service whatever the app does, and the app says why before it asks. BACK steps up
 * one view and leaves the app from the home list, which clears any arm. The service runs on the
 * UI task: a sweep is at most 112 probes of 5 ms, a dump 16 transactions. The row logic is
 * i2c_views.c (pure); this file binds it to the service and the screen.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i2capp.h"
#include "stark_app.h"
#include "stark_event.h"
#include "stark_input.h"
#include "stark_lab.h"
#include "stark_log.h"
#include "ui_dialog.h"
#include "ui_menu.h"

#define SCAN_ROWS_MAX                                                                              \
    24u /* a bus has a handful of devices; the rest is one line saying how many                    \
         */
#define ITEMS_MAX  (SCAN_ROWS_MAX + 2u)
#define LABEL_MAX  12u /* "Device 0x68" */
#define STATUS_MAX 16u
#define NO_SEL     SIZE_MAX

typedef enum { VIEW_HOME, VIEW_SCAN, VIEW_DEVICE, VIEW_READ, VIEW_WRITE, VIEW_DUMP } view_t;
typedef enum { PENDING_NONE, PENDING_WRITE, PENDING_DUMP } pending_t;

enum { HOME_SCAN, HOME_SPEED, HOME_ROWS };
enum { DEV_HEADER, DEV_READ, DEV_DUMP, DEV_WRITE, DEV_ROWS };

static view_t s_view;
static ui_menu_t s_menu;
static bool (*s_menu_event)(stark_screen_t *self, const stark_event_t *e);
static ui_menu_item_t s_items[ITEMS_MAX];
static char s_labels[ITEMS_MAX][LABEL_MAX];
static size_t s_item_count;

static uint32_t s_hz = STARK_LAB_I2C_SPEED_100K;
static stark_lab_scan_t s_scan;
static bool s_scanned;
static size_t s_scan_sel; /* the row opened, so BACK returns to it */
static uint8_t s_addr;

static i2capp_cfg_t s_cfg;
static i2cview_row_t s_rows[I2CAPP_ROWS_MAX];
static size_t s_row_count;
static uint8_t s_result[I2CAPP_LEN_MAX];
static size_t s_result_len;
static char s_status[STATUS_MAX];
static uint8_t s_dump[STARK_LAB_I2C_DUMP_SIZE];
static pending_t s_pending;

static void show(view_t view, size_t sel);
static bool i2c_event(stark_screen_t *self, const stark_event_t *e);
static void i2c_exit(stark_screen_t *self);

static i2capp_edit_t edit_kind(void)
{
    return s_view == VIEW_WRITE ? I2CAPP_EDIT_WRITE : I2CAPP_EDIT_READ;
}

/* ---- rows of each view ------------------------------------------------------------------- */

static void fill_home(void)
{
    s_items[HOME_SCAN] = (ui_menu_item_t){.label = "Scan"};
    s_items[HOME_SPEED] = (ui_menu_item_t){.label = "Speed"};
    s_item_count = HOME_ROWS;
}

static void fill_scan(void)
{
    s_item_count = 0;
    size_t shown = s_scan.n < SCAN_ROWS_MAX ? s_scan.n : SCAN_ROWS_MAX;
    for (size_t i = 0; i < shown; i++) {
        i2cview_addr_text(s_scan.addr[i], s_labels[i], LABEL_MAX);
        s_items[s_item_count++] = (ui_menu_item_t){.label = s_labels[i]};
    }
    if (s_scan.n > shown) {
        (void)snprintf(s_labels[shown], LABEL_MAX, "+%u more", (unsigned)(s_scan.n - shown));
        s_items[s_item_count++] = (ui_menu_item_t){.label = s_labels[shown], .disabled = true};
    }
    if (s_scan.status != STARK_OK) {
        s_items[s_item_count++] = (ui_menu_item_t){.label = "Bus error", .disabled = true};
    } else if (s_scan.n == 0u) {
        s_items[s_item_count++] = (ui_menu_item_t){.label = "No devices", .disabled = true};
    }
}

static void fill_device(void)
{
    (void)snprintf(s_labels[0], LABEL_MAX, "Device 0x%02x", (unsigned)s_addr);
    s_items[DEV_HEADER] = (ui_menu_item_t){.label = s_labels[0], .header = true};
    s_items[DEV_READ] = (ui_menu_item_t){.label = "Read"};
    s_items[DEV_DUMP] = (ui_menu_item_t){.label = "Dump"};
    s_items[DEV_WRITE] = (ui_menu_item_t){.label = "Write"};
    s_item_count = DEV_ROWS;
}

static void fill_edit(void)
{
    s_row_count = i2cview_rows(edit_kind(), &s_cfg, s_result_len, s_rows);
    for (size_t i = 0; i < s_row_count; i++) {
        bool note = s_rows[i].kind == I2CROW_NOTE;
        s_items[i] =
            (ui_menu_item_t){.label = note ? i2cview_note(edit_kind()) : i2cview_label(&s_rows[i]),
                             .disabled = note || s_rows[i].kind == I2CROW_RESULT};
    }
    s_item_count = s_row_count;
}

static void fill_dump(void)
{
    for (size_t i = 0; i < I2CAPP_DUMP_ROWS; i++) {
        i2cview_dump_label(i, s_labels[i], LABEL_MAX);
        s_items[i] = (ui_menu_item_t){.label = s_labels[i]};
    }
    s_item_count = I2CAPP_DUMP_ROWS;
}

static void fill(view_t view)
{
    switch (view) {
        case VIEW_SCAN:
            fill_scan();
            break;
        case VIEW_DEVICE:
            fill_device();
            break;
        case VIEW_READ:
        case VIEW_WRITE:
            fill_edit();
            break;
        case VIEW_DUMP:
            fill_dump();
            break;
        case VIEW_HOME:
        default:
            fill_home();
            break;
    }
}

/* ---- the menu's callbacks ------------------------------------------------------------------ */

static const char *value_of(size_t index, void *ctx)
{
    static char buf[40];
    (void)ctx;
    if (index >= s_item_count) {
        return NULL;
    }
    switch (s_view) {
        case VIEW_HOME:
            if (index == HOME_SPEED) {
                return i2cview_speed_text(s_hz);
            }
            if (s_scanned) {
                (void)snprintf(buf, sizeof buf, "%u found", (unsigned)s_scan.n);
                return buf;
            }
            return NULL;
        case VIEW_SCAN:
            return index < s_scan.n && index < SCAN_ROWS_MAX
                       ? stark_lab_i2c_hint(s_scan.addr[index])
                       : NULL;
        case VIEW_DEVICE:
            return NULL;
        case VIEW_READ:
        case VIEW_WRITE:
            return i2cview_value(&s_rows[index], &s_cfg, s_result, s_result_len, s_status, buf,
                                 sizeof buf);
        case VIEW_DUMP:
            i2cview_dump_value(s_dump, index, buf, sizeof buf);
            return buf;
        default:
            return NULL;
    }
}

/* Moves the selection to `want` (the menu re-initialises its model with the first row selected). */
static void reselect(size_t want)
{
    for (size_t guard = 0;
         guard < s_item_count && want != NO_SEL && ui_menu_model_has_selection(&s_menu.model) &&
         ui_menu_model_selected(&s_menu.model) != want;
         guard++) {
        (void)ui_menu_model_move(&s_menu.model, 1);
    }
}

static void activate(size_t index, void *ctx);
static bool adjust(size_t index, int dir, void *ctx);

/*
 * Re-initialises the one menu for a view. NOTE: the app manager hooks the running screen's on_exit
 * to stop the app (app_manager.c); ui_menu_init() rewrites the whole screen, so the hook is carried
 * over, and the first call installs i2c_exit() as the screen's own, which the manager keeps.
 */
static void show(view_t view, size_t sel)
{
    void (*on_exit)(stark_screen_t *) =
        s_menu.screen.on_exit != NULL ? s_menu.screen.on_exit : i2c_exit;
    s_view = view;
    fill(view);
    /* NOTE: the title stays "I2C": the status bar repaints it only when a screen is pushed. */
    ui_menu_init(&s_menu, "I2C", s_items, s_item_count, activate, NULL);
    s_menu.adjust = (view == VIEW_HOME || view == VIEW_READ || view == VIEW_WRITE) ? adjust : NULL;
    s_menu.value = value_of;
    s_menu_event = s_menu.screen.on_event;
    s_menu.screen.on_event = i2c_event;
    s_menu.screen.on_exit = on_exit;
    reselect(sel);
    stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
}

/*
 * The rows of the current editor changed (a result came in, the register became 16-bit, the byte
 * count moved): rebuild them and keep the selection on the same row, which may have a new index.
 */
static void rebuild(void)
{
    size_t sel =
        ui_menu_model_has_selection(&s_menu.model) ? ui_menu_model_selected(&s_menu.model) : NO_SEL;
    i2cview_row_t was = {I2CROW_NOTE, 0};
    if (sel != NO_SEL && sel < s_row_count) {
        was = s_rows[sel];
    }
    show(s_view, sel);
    for (size_t i = 0; i < s_row_count; i++) {
        if (s_rows[i].kind == was.kind && s_rows[i].idx == was.idx) {
            reselect(i);
            break;
        }
    }
}

/* ---- actions ----------------------------------------------------------------------------- */

static void alert_error(const char *title, stark_err_t err)
{
    char msg[40];
    (void)snprintf(msg, sizeof msg, "%s.", i2cview_error_text(err));
    (void)stark_ui_dialog_alert(title, msg, NULL, NULL);
}

static void do_scan(void)
{
    (void)stark_lab_i2c_scan(&s_scan);
    s_scanned = true;
    if (s_scan.status == STARK_OK) {
        STARK_LOGI("i2capp", "scan %u found", (unsigned)s_scan.n);
    } else {
        STARK_LOGW("i2capp", "scan bus error after %u found", (unsigned)s_scan.n);
    }
    show(VIEW_SCAN, NO_SEL);
}

static void open_device(size_t index)
{
    if (index >= s_scan.n || index >= SCAN_ROWS_MAX) {
        return;
    }
    s_scan_sel = index;
    s_addr = s_scan.addr[index];
    i2cview_cfg_reset(&s_cfg); /* another device: another register map */
    STARK_LOGI("i2capp", "open 0x%02x", (unsigned)s_addr);
    show(VIEW_DEVICE, NO_SEL);
}

static void open_editor(view_t view)
{
    /* NOTE: the register, its width and the nibble carry over from one editor to the other (and
     * across the dump): write a register, then read it back with no setting up again. */
    s_result_len = 0;
    s_status[0] = '\0';
    show(view, NO_SEL);
}

/* The service refused (or the app already knows it will): the reason, in a log line and an alert.
 */
static void refused(const char *what, const char *why)
{
    STARK_LOGW("i2capp", "%s refused: %s", what, why);
    char msg[64];
    (void)snprintf(msg, sizeof msg, "%s refused: %s.", what, why);
    (void)stark_ui_dialog_alert("Refused", msg, NULL, NULL);
}

static void do_read(void)
{
    char reg[8];
    i2cview_reg_text(s_cfg.reg, s_cfg.reg16, reg, sizeof reg);
    stark_err_t err = stark_lab_i2c_read(s_addr, s_cfg.reg, s_cfg.reg16, s_result, s_cfg.len, s_hz);
    if (err == STARK_OK) {
        char data[3u * I2CAPP_LEN_MAX];
        (void)stark_lab_i2c_hex(s_result, s_cfg.len, data, sizeof data);
        s_result_len = s_cfg.len;
        s_status[0] = '\0';
        STARK_LOGI("i2capp", "read 0x%02x reg %s len %u: %s", (unsigned)s_addr, reg,
                   (unsigned)s_cfg.len, data);
    } else {
        s_result_len = 0;
        (void)snprintf(s_status, sizeof s_status, "%s", i2cview_error_text(err));
        const char *why = stark_lab_i2c_refusal(s_addr, s_cfg.reg16, false);
        STARK_LOGW("i2capp", "read 0x%02x reg %s failed: %s", (unsigned)s_addr, reg,
                   stark_err_str(err));
        if (err == STARK_ERR_NOT_SUPPORTED && why != NULL) {
            refused("Read", why);
        }
    }
    rebuild();
}

/* The confirmation was answered: OK arms exactly this call and makes it, BACK does nothing. */
static void confirmed(stark_dialog_result_t result, void *ctx)
{
    (void)ctx;
    pending_t what = s_pending;
    s_pending = PENDING_NONE;
    if (result != STARK_DIALOG_OK) {
        STARK_LOGI("i2capp", "cancelled %s 0x%02x", what == PENDING_DUMP ? "dump" : "write",
                   (unsigned)s_addr);
        return;
    }
    if (what == PENDING_DUMP) {
        (void)stark_lab_i2c_arm(stark_lab_i2c_dump_fingerprint(s_addr));
        stark_err_t err = stark_lab_i2c_dump(s_addr, s_dump, s_hz);
        if (err == STARK_OK) {
            STARK_LOGI("i2capp", "dump 0x%02x ok", (unsigned)s_addr);
            show(VIEW_DUMP, NO_SEL);
        } else {
            STARK_LOGW("i2capp", "dump 0x%02x failed: %s", (unsigned)s_addr, stark_err_str(err));
            alert_error("Dump failed", err);
        }
    } else if (what == PENDING_WRITE) {
        (void)stark_lab_i2c_arm(stark_lab_i2c_write_fingerprint(s_addr, s_cfg.reg, s_cfg.reg16,
                                                                s_cfg.data, s_cfg.count));
        stark_err_t err =
            stark_lab_i2c_write(s_addr, s_cfg.reg, s_cfg.reg16, s_cfg.data, s_cfg.count, s_hz);
        char reg[8];
        char data[3u * I2CAPP_WRITE_MAX];
        i2cview_reg_text(s_cfg.reg, s_cfg.reg16, reg, sizeof reg);
        (void)stark_lab_i2c_hex(s_cfg.data, s_cfg.count, data, sizeof data);
        (void)snprintf(s_status, sizeof s_status, "%s",
                       err == STARK_OK ? "ok" : i2cview_error_text(err));
        STARK_LOGI("i2capp", "write 0x%02x reg %s: %s %s", (unsigned)s_addr, reg, data,
                   err == STARK_OK ? "ok" : stark_err_str(err));
        if (err != STARK_OK) {
            alert_error("Write failed", err);
        }
        rebuild();
    }
}

/* A dump or a write: refused up front when the service would refuse it, else asked. */
static void request(pending_t what)
{
    const char *why = stark_lab_i2c_refusal(s_addr, s_cfg.reg16, what == PENDING_WRITE);
    char title[24];
    char msg[160];
    if (why != NULL) {
        /* NOTE: the call is made anyway, unarmed: the service refuses it, which is the log line
         * `lab: i2c ... refused` and proof that the rule is its own and not only the app's. */
        if (what == PENDING_WRITE) {
            (void)stark_lab_i2c_write(s_addr, s_cfg.reg, s_cfg.reg16, s_cfg.data, s_cfg.count,
                                      s_hz);
        } else {
            (void)stark_lab_i2c_dump(s_addr, s_dump, s_hz);
        }
        refused(what == PENDING_WRITE ? "Write" : "Dump", why);
        return;
    }
    if (what == PENDING_WRITE) {
        (void)snprintf(title, sizeof title, "Write 0x%02x?", (unsigned)s_addr);
        i2cview_write_message(s_addr, &s_cfg, msg, sizeof msg);
    } else {
        (void)snprintf(title, sizeof title, "Dump 0x%02x?", (unsigned)s_addr);
        i2cview_dump_message(s_addr, msg, sizeof msg);
    }
    STARK_LOGI("i2capp", "confirm %s 0x%02x", what == PENDING_WRITE ? "write" : "dump",
               (unsigned)s_addr);
    s_pending = what;
    if (stark_ui_dialog_confirm(title, msg, confirmed, NULL) != STARK_OK) {
        s_pending = PENDING_NONE;
    }
}

static void activate(size_t index, void *ctx)
{
    (void)ctx;
    switch (s_view) {
        case VIEW_HOME:
            if (index == HOME_SCAN) {
                do_scan();
            }
            break;
        case VIEW_SCAN:
            open_device(index);
            break;
        case VIEW_DEVICE:
            if (index == DEV_READ) {
                open_editor(VIEW_READ);
            } else if (index == DEV_DUMP) {
                request(PENDING_DUMP);
            } else if (index == DEV_WRITE) {
                open_editor(VIEW_WRITE);
            }
            break;
        case VIEW_READ:
        case VIEW_WRITE:
            if (index < s_row_count && s_rows[index].kind == I2CROW_READ) {
                do_read();
            } else if (index < s_row_count && s_rows[index].kind == I2CROW_WRITE) {
                request(PENDING_WRITE);
            }
            break;
        case VIEW_DUMP:
        default:
            break;
    }
}

/* LEFT/RIGHT: the speed at home, a value in the editors. */
static bool adjust(size_t index, int dir, void *ctx)
{
    (void)ctx;
    if (s_view == VIEW_HOME) {
        if (index != HOME_SPEED) {
            return false;
        }
        s_hz = i2cview_speed_step(s_hz);
        STARK_LOGI("i2capp", "speed %s", i2cview_speed_text(s_hz));
        stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
        return true;
    }
    if (index >= s_row_count || !i2cview_adjust(&s_cfg, &s_rows[index], dir)) {
        return false;
    }
    s_status[0] = '\0'; /* a result belongs to the register it was read from */
    s_result_len = 0;
    rebuild();
    return true;
}

/* BACK steps up one view; at home it is left to stark_ui (the app stops). */
static bool back(void)
{
    switch (s_view) {
        case VIEW_SCAN:
            show(VIEW_HOME, NO_SEL);
            return true;
        case VIEW_DEVICE:
            show(VIEW_SCAN, s_scan_sel);
            return true;
        case VIEW_READ:
        case VIEW_WRITE:
        case VIEW_DUMP:
            stark_lab_i2c_disarm();
            show(VIEW_DEVICE, NO_SEL);
            return true;
        case VIEW_HOME:
        default:
            return false;
    }
}

static bool i2c_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type == STARK_EVT_KEY && e->key.key == STARK_KEY_BACK &&
        e->key.action == STARK_KEY_SHORT && back()) {
        return true;
    }
    return s_menu_event(self, e);
}

/* Before the manager joins the worker (there is none): nothing armed outlives the screen. */
static void i2c_exit(stark_screen_t *self)
{
    (void)self;
    stark_lab_i2c_disarm();
}

static stark_err_t i2c_start(void **state)
{
    (void)state;
    s_view = VIEW_HOME;
    s_pending = PENDING_NONE;
    s_scanned = false;
    s_scan = (stark_lab_scan_t){0};
    s_hz = STARK_LAB_I2C_SPEED_100K;
    return STARK_OK;
}

static void i2c_stop(void *state)
{
    (void)state;
    stark_lab_i2c_disarm();
}

static stark_screen_t *i2c_screen(void *state)
{
    (void)state;
    STARK_LOGI("i2capp", "home");
    show(VIEW_HOME, NO_SEL);
    return &s_menu.screen;
}

/* Launcher icon (16x16, 1bpp — ui_menu.h): a clock and a data line. */
static const uint8_t k_icon[32] = {
    0x00, 0x00, 0x00, 0x00, 0x3F, 0xFC, 0x20, 0x04, 0x20, 0x04, 0xE0, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x7C, 0x1F, 0x44, 0x11, 0x44, 0x11, 0xC7, 0xF1, 0x00, 0x00, 0x00, 0x00,
};

const stark_app_t app_i2c = {
    .id = "i2c",
    .title = "I2C",
    .category = "Lab",
    .on_start = i2c_start,
    .on_stop = i2c_stop,
    .screen = i2c_screen,
    .icon = k_icon,
    .caps_required = 0,
};
