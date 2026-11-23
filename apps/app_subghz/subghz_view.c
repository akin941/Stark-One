/*
 * subghz_view.c — the Sub-GHz app's rows (subghzapp.h): the meter's frequency, level and
 * bar, the scan's progress, peak and level, the capture's setup, progress and timing
 * histogram, the saved captures and one of them read back, or the one notice row when
 * there is no module. Text is built from stark_subghz's status each time; nothing stale
 * is ever kept.
 */
#include <stdio.h>
#include <string.h>
#include "stark_subghz.h"
#include "subghz_rssi.h"
#include "subghzapp.h"

#define ROWS_MAX     18 /* a file view: 6 rows, 8 bins, the rest, and the list's 16 */
#define VALUE_SIZE   24
#define LABEL_SIZE   20
#define BAR_CELLS    12
#define BAR_FLOOR_DB (-110) /* an empty bar */
#define BAR_SPAN_DB  80     /* a full bar: -30 dBm */
#define NO_ROW       ((size_t) - 1)

static stark_subghz_status_t s_st;
static bool s_ready;
static bool s_seen_ready; /* a module was ready at some point in this session */
static uint32_t s_hz = SGAPP_DEFAULT_HZ;
static subghz_mod_t s_mod = SUBGHZ_MOD_OOK;
static ui_menu_item_t s_items[ROWS_MAX];
static char s_values[ROWS_MAX][VALUE_SIZE];
static char s_labels[ROWS_MAX][LABEL_SIZE];
static bool s_has_value[ROWS_MAX];
static size_t s_count;
static size_t s_again_row = NO_ROW;
static size_t s_start_row = NO_ROW;
static size_t s_stop_row = NO_ROW;
static size_t s_save_row = NO_ROW;
static size_t s_restart_row = NO_ROW;
static sgapp_mode_t s_mode;
static char s_file_name[16];
static subghz_raw_t s_file;

void sgview_reset(void)
{
    s_hz = SGAPP_DEFAULT_HZ;
    s_mod = SUBGHZ_MOD_OOK;
    s_seen_ready = false;
    memset(&s_file, 0, sizeof s_file);
    s_file_name[0] = '\0';
}

void sgview_refresh(void)
{
    s_ready = stark_subghz_ready();
    s_seen_ready = s_seen_ready || s_ready;
    stark_subghz_status(&s_st);
}

bool sgview_ready(void)
{
    return s_ready;
}

uint32_t sgview_hz(void)
{
    return s_hz;
}

bool sgview_step_hz(int dir)
{
    uint32_t hz = s_hz;
    if (dir > 0) {
        hz = s_hz + SUBGHZ_METER_STEP_HZ > SUBGHZ_BAND_MAX_HZ ? SUBGHZ_BAND_MAX_HZ
                                                              : s_hz + SUBGHZ_METER_STEP_HZ;
    } else if (dir < 0) {
        hz = s_hz < SUBGHZ_BAND_MIN_HZ + SUBGHZ_METER_STEP_HZ ? SUBGHZ_BAND_MIN_HZ
                                                              : s_hz - SUBGHZ_METER_STEP_HZ;
    }
    bool moved = hz != s_hz;
    s_hz = hz;
    return moved;
}

subghz_mod_t sgview_mod(void)
{
    return s_mod;
}

void sgview_toggle_mod(void)
{
    s_mod = s_mod == SUBGHZ_MOD_OOK ? SUBGHZ_MOD_2FSK : SUBGHZ_MOD_OOK;
}

sgcap_phase_t sgview_cap_phase(void)
{
    if (s_st.mode != STARK_SUBGHZ_CAPTURE) {
        return SGCAP_SETUP;
    }
    return s_st.capture_done ? SGCAP_DONE : SGCAP_RUNNING;
}

bool sgview_capture_raw(subghz_raw_t *out, subghz_sym_t *symbols)
{
    *out = (subghz_raw_t){s_st.hz, s_mod, SUBGHZ_RAW_RATE_DEFAULT, symbols, s_st.symbols};
    return s_st.mode == STARK_SUBGHZ_CAPTURE && s_st.capture_done && subghz_raw_valid(out);
}

void sgview_set_file(const char *name, const subghz_raw_t *raw)
{
    (void)snprintf(s_file_name, sizeof s_file_name, "%s", name);
    s_file = *raw;
}

/* ---- rows ---------------------------------------------------------------------------- */

/* A row with its value text (NULL: none); `enabled` rows can be selected. */
static size_t row(const char *label, bool enabled)
{
    s_items[s_count] = (ui_menu_item_t){.label = label, .disabled = !enabled};
    s_has_value[s_count] = false;
    return s_count++;
}

/* The same, with a label built here. */
static size_t row_f(bool enabled, const char *fmt, unsigned a)
{
    (void)snprintf(s_labels[s_count], LABEL_SIZE, fmt, a);
    return row(s_labels[s_count], enabled);
}

static void value_mhz(size_t r, uint32_t hz)
{
    char mhz[12];
    subghz_mhz_text(mhz, sizeof mhz, hz);
    (void)snprintf(s_values[r], VALUE_SIZE, "%s MHz", mhz);
    s_has_value[r] = true;
}

static void value_dbm(size_t r, int16_t dbm_x2)
{
    char dbm[12];
    subghz_dbm_text(dbm, sizeof dbm, dbm_x2);
    (void)snprintf(s_values[r], VALUE_SIZE, "%s dBm", dbm);
    s_has_value[r] = true;
}

static void value_text(size_t r, const char *text)
{
    (void)snprintf(s_values[r], VALUE_SIZE, "%s", text);
    s_has_value[r] = true;
}

static void value_unsigned(size_t r, const char *fmt, unsigned long v)
{
    (void)snprintf(s_values[r], VALUE_SIZE, fmt, v);
    s_has_value[r] = true;
}

/* A bar from -110 dBm (empty) to -30 dBm (full). */
static void value_bar(size_t r, int16_t dbm_x2)
{
    int level = (int)dbm_x2 / 2 - BAR_FLOOR_DB; /* dB above the floor */
    int cells = (level * BAR_CELLS + BAR_SPAN_DB / 2) / BAR_SPAN_DB;
    cells = cells < 0 ? 0 : (cells > BAR_CELLS ? BAR_CELLS : cells);
    for (int i = 0; i < BAR_CELLS; i++) {
        s_values[r][i] = i < cells ? '#' : '.';
    }
    s_values[r][BAR_CELLS] = '\0';
    s_has_value[r] = true;
}

static void meter_rows(void)
{
    bool live = s_st.mode == STARK_SUBGHZ_METER && s_st.valid;
    value_mhz(row("Frequency", true), s_hz);
    size_t level = row("RSSI", false);
    size_t bar = row("Level", false);
    if (live) {
        value_dbm(level, s_st.dbm_x2);
        value_bar(bar, s_st.dbm_x2);
    } else {
        value_text(level, "...");
        value_text(bar, "...");
    }
}

static void scan_rows(void)
{
    if (s_st.mode == STARK_SUBGHZ_SCAN && s_st.scan_done) {
        value_mhz(row("Peak", false), s_st.hz);
        value_dbm(row("Level", false), s_st.dbm_x2);
        s_again_row = row("Scan again", true);
    } else {
        value_text(row("Scanning", false), "...");
    }
}

/* "Time" / "Duration" in ms with one decimal below 10 ms. */
static void value_ms(size_t r, uint32_t us)
{
    if (us < 10000u) {
        (void)snprintf(s_values[r], VALUE_SIZE, "%lu.%lu ms", (unsigned long)(us / 1000u),
                       (unsigned long)((us % 1000u) / 100u));
    } else {
        (void)snprintf(s_values[r], VALUE_SIZE, "%lu ms", (unsigned long)((us + 500u) / 1000u));
    }
    s_has_value[r] = true;
}

/* The timing histogram of the symbols: one row per bin (mean µs, how many) and `other`. */
static void hist_rows(const subghz_sym_t *sym, size_t n)
{
    subghz_hist_t h;
    subghz_hist(sym, n, &h);
    for (unsigned b = 0; b < h.bins; b++) {
        size_t r = row_f(false, "%u us", (unsigned)h.bin[b].mean_us);
        value_unsigned(r, "%lu sym", h.bin[b].count);
    }
    if (h.other > 0) {
        value_unsigned(row("Longer", false), "%lu sym", h.other);
    }
}

static void capture_rows(void)
{
    switch (sgview_cap_phase()) {
        case SGCAP_SETUP:
            value_mhz(row("Frequency", true), s_hz);
            value_text(row("Modulation", true), s_mod == SUBGHZ_MOD_OOK ? "OOK" : "2-FSK");
            s_start_row = row("Start", true);
            break;
        case SGCAP_RUNNING:
            value_unsigned(row("Symbols", false), "%lu", s_st.symbols);
            value_ms(row("Time", false), s_st.span_us);
            s_stop_row = row("Stop", true);
            break;
        case SGCAP_DONE:
        default: {
            value_unsigned(row("Symbols", false), "%lu", s_st.symbols);
            value_ms(row("Duration", false), s_st.span_us);
            hist_rows(sgfile_symbols(), sgfile_symbols() != NULL ? s_st.symbols : 0u);
            s_save_row = row("Save", !sgfile_busy());
            if (sgfile_busy()) {
                value_text(s_save_row, "saving...");
            } else if (sgfile_saved_name()[0] != '\0') {
                value_text(s_save_row, sgfile_saved_name());
            }
            s_restart_row = row("Capture again", !sgfile_busy());
            break;
        }
    }
}

static void saved_rows(void)
{
    size_t n = 0;
    const sgfile_entry_t *list = sgfile_entries(&n);
    if (n == 0) {
        (void)row("No captures", false);
    }
    for (size_t i = 0; i < n; i++) {
        value_unsigned(row(list[i].name, !sgfile_busy()), "%lu B", list[i].size);
    }
}

static void file_rows(void)
{
    value_text(row("File", false), s_file_name);
    value_mhz(row("Frequency", false), s_file.freq_hz);
    value_text(row("Modulation", false), s_file.mod == SUBGHZ_MOD_OOK ? "OOK" : "2-FSK");
    value_unsigned(row("Rate", false), "%lu Bd", s_file.rate);
    value_unsigned(row("Symbols", false), "%lu", s_file.n);
    value_ms(row("Duration", false), subghz_raw_duration_us(&s_file));
    hist_rows(s_file.sym, s_file.n);
}

const ui_menu_item_t *sgview_items(sgapp_mode_t mode, size_t *count)
{
    s_count = 0;
    s_again_row = s_start_row = s_stop_row = s_save_row = s_restart_row = NO_ROW;
    s_mode = mode;
    if (!s_ready) {
        bool lost = s_seen_ready;
        value_text(row(lost ? "Sub-GHz module lost" : "No Sub-GHz module", false),
                   lost ? "retrying" : "absent");
        s_mode = SGAPP_HOME;
    } else if (mode == SGAPP_METER) {
        meter_rows();
    } else if (mode == SGAPP_SCAN) {
        scan_rows();
    } else if (mode == SGAPP_CAPTURE) {
        capture_rows();
    } else if (mode == SGAPP_SAVED) {
        saved_rows();
    } else if (mode == SGAPP_FILE) {
        file_rows();
    } else {
        value_mhz(row("RSSI meter", true), s_hz);
        (void)row("Band scan", true);
        (void)row("Capture", true);
        (void)row("Saved", true);
    }
    *count = s_count;
    return s_items;
}

const char *sgview_value(size_t index)
{
    return index < s_count && s_has_value[index] ? s_values[index] : NULL;
}

bool sgview_is_meter_row(size_t index)
{
    return s_ready && s_mode == SGAPP_HOME && index == 0;
}

bool sgview_is_scan_row(size_t index)
{
    return s_ready && s_mode == SGAPP_HOME && index == 1;
}

bool sgview_is_capture_row(size_t index)
{
    return s_ready && s_mode == SGAPP_HOME && index == 2;
}

bool sgview_is_saved_row(size_t index)
{
    return s_ready && s_mode == SGAPP_HOME && index == 3;
}

bool sgview_is_again_row(size_t index)
{
    return index == s_again_row;
}

bool sgview_is_start_row(size_t index)
{
    return index == s_start_row;
}

bool sgview_is_stop_row(size_t index)
{
    return index == s_stop_row;
}

bool sgview_is_save_row(size_t index)
{
    return index == s_save_row;
}

bool sgview_is_restart_row(size_t index)
{
    return index == s_restart_row;
}

void sgview_hist_text(const subghz_hist_t *h, char *out, size_t size)
{
    size_t used = 0;
    out[0] = '\0';
    for (unsigned b = 0; b < h->bins && used < size; b++) {
        int n = snprintf(out + used, size - used, "%s%lux%lu", b > 0 ? " " : "",
                         (unsigned long)h->bin[b].mean_us, (unsigned long)h->bin[b].count);
        used += n > 0 ? (size_t)n : 0u;
    }
    if (h->other > 0 && used < size) {
        (void)snprintf(out + used, size - used, " other=%lu", (unsigned long)h->other);
    }
}
