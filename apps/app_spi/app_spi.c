/*
 * app_spi.c — SPI (TASKS.md STARK-0706, ADR-0023 §6): one bounded, confirmed SPI transaction on a
 * module slot whose module declares SPI and no function. Home lists both slots with the module's
 * name or the reason there is no probe (`no module`, `owned by Sub-GHz`, `no chip select`, ...);
 * OK on a slot that can be probed opens Setup (Clock, Mode, three presets, a byte editor), and Send
 * asks a confirmation that names the slot, clock, mode and bytes ("Own hardware only"); OK makes
 * that one transaction and shows what came back as hex and ASCII, BACK sends nothing. A slot that
 * belongs to a driver (the radio, the NFC reader, anything unknown) is refused by the service
 * whatever the app does, so the probe can never write to a radio's PATABLE or strobe its
 * transmitter around the transmit policy (ADR-0022 §6). One transaction per confirmation: nothing
 * repeats. BACK steps up one view and leaves the app from the home list, which forgets any request.
 * The service runs on the UI task: a transaction of 32 bytes at 100 kHz is 2.6 ms. The row logic is
 * spi_views.c (pure); this file binds it to the service and the screen.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spiapp.h"
#include "stark_app.h"
#include "stark_event.h"
#include "stark_input.h"
#include "stark_lab.h"
#include "stark_log.h"
#include "stark_module.h"
#include "ui_dialog.h"
#include "ui_menu.h"

#define STATUS_MAX 16u
#define NO_SEL     SIZE_MAX

typedef enum { VIEW_HOME, VIEW_SETUP } view_t;

static view_t s_view;
static ui_menu_t s_menu;
static bool (*s_menu_event)(stark_screen_t *self, const stark_event_t *e);
static size_t s_item_count;
static size_t s_slot_count;
static uint8_t s_slot;
static size_t s_row_count;
static size_t s_result_len;
static bool s_asking;

/*
 * Everything the menu points into, held on the heap while the app runs (about 0.9 kB on the
 * board) and given back by on_stop(): the longest editor and answer need 49 rows, and none of it
 * may cost the boot heap while no one is probing (ADR-0023 section 10: transient buffers are heap,
 * held only while the app runs, like the GPIO app's edge ring).
 */
typedef struct {
    ui_menu_item_t items[SPIAPP_ROWS_MAX];
    spiview_row_t rows[SPIAPP_ROWS_MAX];
    stark_lab_spi_slot_t slots[STARK_LAB_SPI_SLOTS];
    spiapp_cfg_t cfg;
    stark_lab_spi_req_t req;
    uint8_t rx[SPIAPP_BYTES_MAX];
    char status[STATUS_MAX];
} spi_state_t;

static spi_state_t *s_st;

static void show(view_t view, size_t sel);
static bool spi_event(stark_screen_t *self, const stark_event_t *e);
static void spi_exit(stark_screen_t *self);

/* ---- rows of each view ------------------------------------------------------------------- */

/* The slots as they are now: the home list is redrawn from this, and OK acts on it. */
static void refresh_slots(void)
{
    s_slot_count = stark_lab_spi_slots(s_st->slots, STARK_LAB_SPI_SLOTS);
}

static void fill_home(void)
{
    static const char *const k_labels[STARK_LAB_SPI_SLOTS] = {"Slot 0", "Slot 1"};
    refresh_slots();
    for (size_t i = 0; i < s_slot_count && i < STARK_LAB_SPI_SLOTS; i++) {
        s_st->items[i] = (ui_menu_item_t){.label = k_labels[i]};
    }
    s_item_count = s_slot_count;
}

static void fill_setup(void)
{
    s_row_count = spiview_rows(&s_st->cfg, s_result_len, s_st->rows);
    for (size_t i = 0; i < s_row_count; i++) {
        bool note = s_st->rows[i].kind == SPIROW_NOTE;
        s_st->items[i] = (ui_menu_item_t){
            .label = note ? spiview_note() : spiview_label(&s_st->rows[i]), .disabled = note};
    }
    s_item_count = s_row_count;
}

/* ---- the menu's callbacks ------------------------------------------------------------------ */

static const char *value_of(size_t index, void *ctx)
{
    static char buf[28]; /* eight bytes of hex are 23 characters */
    (void)ctx;
    if (index >= s_item_count) {
        return NULL;
    }
    if (s_view == VIEW_HOME) {
        return index < s_slot_count ? spiview_slot_value(&s_st->slots[index]) : NULL;
    }
    return spiview_value(&s_st->rows[index], &s_st->cfg, s_st->rx, s_result_len, s_st->status, buf,
                         sizeof buf);
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
 * over, and the first call installs spi_exit() as the screen's own, which the manager keeps.
 */
static void show(view_t view, size_t sel)
{
    void (*on_exit)(stark_screen_t *) =
        s_menu.screen.on_exit != NULL ? s_menu.screen.on_exit : spi_exit;
    s_view = view;
    if (view == VIEW_HOME) {
        fill_home();
    } else {
        fill_setup();
    }
    /* NOTE: the title stays "SPI": the status bar repaints it only when a screen is pushed. */
    ui_menu_init(&s_menu, "SPI", s_st->items, s_item_count, activate, NULL);
    s_menu.adjust = view == VIEW_SETUP ? adjust : NULL;
    s_menu.value = value_of;
    s_menu_event = s_menu.screen.on_event;
    s_menu.screen.on_event = spi_event;
    s_menu.screen.on_exit = on_exit;
    reselect(sel);
    stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
}

/* The index of the first row of `kind` (the selection follows the row, not the index). */
static size_t index_of(spiview_kind_t kind)
{
    for (size_t i = 0; i < s_row_count; i++) {
        if (s_st->rows[i].kind == kind) {
            return i;
        }
    }
    return NO_SEL;
}

/*
 * The rows of the setup changed (a result came in, the byte count moved, a preset loaded): rebuild
 * them and keep the selection on the same row, which may have a new index; `want` overrides it.
 */
static void rebuild(size_t want)
{
    size_t sel =
        ui_menu_model_has_selection(&s_menu.model) ? ui_menu_model_selected(&s_menu.model) : NO_SEL;
    spiview_row_t was = {SPIROW_NOTE, 0};
    if (sel != NO_SEL && sel < s_row_count) {
        was = s_st->rows[sel];
    }
    show(VIEW_SETUP, want == NO_SEL ? sel : want);
    if (want != NO_SEL) {
        return;
    }
    for (size_t i = 0; i < s_row_count; i++) {
        if (s_st->rows[i].kind == was.kind && s_st->rows[i].idx == was.idx) {
            reselect(i);
            break;
        }
    }
}

/* ---- actions ----------------------------------------------------------------------------- */

static void alert(const char *title, const char *text)
{
    char msg[48];
    (void)snprintf(msg, sizeof msg, "%s.", text);
    (void)stark_ui_dialog_alert(title, msg, NULL, NULL);
}

static void open_slot(size_t index)
{
    refresh_slots(); /* NOTE: act on the slot as it is now, not as the list last drew it */
    if (index >= s_slot_count) {
        return;
    }
    if (!s_st->slots[index].eligible) {
        STARK_LOGW("spiapp", "slot %u refused: %s", (unsigned)s_st->slots[index].slot,
                   s_st->slots[index].why);
        char msg[56];
        (void)snprintf(msg, sizeof msg, "Slot %u: %s.", (unsigned)s_st->slots[index].slot,
                       s_st->slots[index].why);
        (void)stark_ui_dialog_alert("No probe", msg, NULL, NULL);
        return;
    }
    s_slot = s_st->slots[index].slot;
    s_result_len = 0;
    s_st->status[0] = '\0';
    STARK_LOGI("spiapp", "open slot %u", (unsigned)s_slot);
    show(VIEW_SETUP, NO_SEL);
}

/* The confirmation was answered: OK makes the one transaction that was asked about, BACK none. */
static void confirmed(stark_dialog_result_t result, void *ctx)
{
    (void)ctx;
    s_asking = false;
    if (result != STARK_DIALOG_OK) {
        stark_lab_spi_cancel();
        STARK_LOGI("spiapp", "cancelled slot %u", (unsigned)s_st->req.slot);
        return;
    }
    stark_err_t err = stark_lab_spi_send(&s_st->req, s_st->rx);
    if (err == STARK_OK) {
        char data[3u * SPIAPP_BYTES_MAX];
        (void)stark_lab_i2c_hex(s_st->rx, s_st->req.n, data, sizeof data);
        s_result_len = s_st->req.n;
        (void)snprintf(s_st->status, sizeof s_st->status, "ok");
        STARK_LOGI("spiapp", "sent slot %u: rx %s", (unsigned)s_st->req.slot, data);
        s_row_count = spiview_rows(&s_st->cfg, s_result_len, s_st->rows);
        rebuild(index_of(SPIROW_HEX)); /* NOTE: the answer is the point: bring it into view */
        return;
    }
    s_result_len = 0;
    (void)snprintf(s_st->status, sizeof s_st->status, "%s", spiview_error_text(err));
    STARK_LOGW("spiapp", "send slot %u failed: %s", (unsigned)s_st->req.slot, stark_err_str(err));
    alert("Send failed", spiview_error_text(err));
    rebuild(NO_SEL);
}

/* Records what the dialog will show, then asks: the request is the service's arm. */
static void request_send(void)
{
    stark_err_t err = stark_lab_spi_request(s_slot, spiview_hz(&s_st->cfg), s_st->cfg.mode,
                                            s_st->cfg.data, s_st->cfg.count, &s_st->req);
    if (err != STARK_OK) {
        s_result_len = 0;
        (void)snprintf(s_st->status, sizeof s_st->status, "%s", spiview_error_text(err));
        STARK_LOGW("spiapp", "request slot %u refused: %s", (unsigned)s_slot, stark_err_str(err));
        alert("Refused", spiview_error_text(err));
        rebuild(NO_SEL);
        return;
    }
    char msg[160];
    spiview_message(s_slot, &s_st->cfg, msg, sizeof msg);
    STARK_LOGI("spiapp", "confirm slot %u %s mode %u", (unsigned)s_slot,
               stark_lab_spi_clock_text(s_st->req.hz), (unsigned)s_st->req.mode);
    s_asking = true;
    if (stark_ui_dialog_confirm("Send SPI?", msg, confirmed, NULL) != STARK_OK) {
        s_asking = false;
        stark_lab_spi_cancel();
    }
}

static void activate(size_t index, void *ctx)
{
    (void)ctx;
    if (s_view == VIEW_HOME) {
        open_slot(index);
        return;
    }
    if (index >= s_row_count) {
        return;
    }
    if (s_st->rows[index].kind == SPIROW_PRESET) {
        (void)spiview_preset(&s_st->cfg, s_st->rows[index].idx);
        s_result_len = 0;
        s_st->status[0] = '\0';
        STARK_LOGI("spiapp", "preset %s", spiview_label(&s_st->rows[index]));
        s_row_count = spiview_rows(&s_st->cfg, 0, s_st->rows);
        rebuild(index_of(SPIROW_SEND)); /* the next thing to do is send it */
    } else if (s_st->rows[index].kind == SPIROW_SEND) {
        request_send();
    }
}

/* LEFT/RIGHT: a value in the setup. */
static bool adjust(size_t index, int dir, void *ctx)
{
    (void)ctx;
    if (index >= s_row_count || !spiview_adjust(&s_st->cfg, &s_st->rows[index], dir)) {
        return false;
    }
    s_st->status[0] = '\0'; /* a result belongs to the bytes it answered */
    s_result_len = 0;
    stark_lab_spi_cancel();
    rebuild(NO_SEL);
    return true;
}

/* BACK steps up one view; at home it is left to stark_ui (the app stops). */
static bool back(void)
{
    if (s_view == VIEW_SETUP) {
        stark_lab_spi_cancel();
        show(VIEW_HOME, NO_SEL);
        return true;
    }
    return false;
}

static bool spi_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type == STARK_EVT_SYSTEM && e->system.id == STARK_SYS_MODULE && s_view == VIEW_HOME) {
        size_t sel = ui_menu_model_has_selection(&s_menu.model)
                         ? ui_menu_model_selected(&s_menu.model)
                         : NO_SEL;
        show(VIEW_HOME, sel); /* a module came or went: the list says so */
        return true;
    }
    if (e->type == STARK_EVT_KEY && e->key.key == STARK_KEY_BACK &&
        e->key.action == STARK_KEY_SHORT && !s_asking && back()) {
        return true;
    }
    return s_menu_event(self, e);
}

/* Before the manager joins the worker (there is none): nothing asked outlives the screen. */
static void spi_exit(stark_screen_t *self)
{
    (void)self;
    stark_lab_spi_cancel();
}

static stark_err_t spi_start(void **state)
{
    s_st = calloc(1, sizeof *s_st);
    if (s_st == NULL) {
        return STARK_ERR_NO_MEM;
    }
    *state = s_st;
    s_view = VIEW_HOME;
    s_asking = false;
    s_result_len = 0;
    spiview_cfg_reset(&s_st->cfg);
    return STARK_OK;
}

static void spi_stop(void *state)
{
    (void)state;
    stark_lab_spi_cancel();
    free(s_st);
    s_st = NULL;
}

static stark_screen_t *spi_screen(void *state)
{
    (void)state;
    STARK_LOGI("spiapp", "home");
    show(VIEW_HOME, NO_SEL);
    return &s_menu.screen;
}

/* Launcher icon (16x16, 1bpp — ui_menu.h): a chip with two pins out of each side. */
static const uint8_t k_icon[32] = {
    0x00, 0x00, 0x00, 0x00, 0x24, 0x24, 0x24, 0x24, 0x3F, 0xFC, 0x20, 0x04, 0x2A, 0x54, 0x20, 0x04,
    0x2A, 0x54, 0x20, 0x04, 0x3F, 0xFC, 0x24, 0x24, 0x24, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

const stark_app_t app_spi = {
    .id = "spi",
    .title = "SPI",
    .category = "Lab",
    .on_start = spi_start,
    .on_stop = spi_stop,
    .screen = spi_screen,
    .icon = k_icon,
    .caps_required = 0,
};
