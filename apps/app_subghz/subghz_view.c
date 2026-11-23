/*
 * subghz_view.c — the Sub-GHz app's rows (subghzapp.h): the meter's frequency,
 * level and bar, the scan's progress, peak and level, or the one notice row when
 * there is no module. Text is built from stark_subghz's status each time; nothing
 * stale is ever kept.
 */
#include <stdio.h>
#include "stark_subghz.h"
#include "subghz_rssi.h"
#include "subghzapp.h"

#define ROWS_MAX     3
#define VALUE_SIZE   24
#define BAR_CELLS    12
#define BAR_FLOOR_DB (-110) /* an empty bar */
#define BAR_SPAN_DB  80     /* a full bar: -30 dBm */

static stark_subghz_status_t s_st;
static bool s_ready;
static bool s_seen_ready; /* a module was ready at some point in this session */
static uint32_t s_hz = SGAPP_DEFAULT_HZ;
static ui_menu_item_t s_items[ROWS_MAX];
static char s_values[ROWS_MAX][VALUE_SIZE];
static bool s_has_value[ROWS_MAX];
static size_t s_count;
static size_t s_again_row = (size_t)-1;
static sgapp_mode_t s_mode;

void sgview_reset(void)
{
    s_hz = SGAPP_DEFAULT_HZ;
    s_seen_ready = false;
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

/* A row with its value text (NULL: none); `enabled` rows can be selected. */
static size_t row(const char *label, bool enabled)
{
    s_items[s_count] = (ui_menu_item_t){.label = label, .disabled = !enabled};
    s_has_value[s_count] = false;
    return s_count++;
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

const ui_menu_item_t *sgview_items(sgapp_mode_t mode, size_t *count)
{
    s_count = 0;
    s_again_row = (size_t)-1;
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
    } else {
        value_mhz(row("RSSI meter", true), s_hz);
        (void)row("Band scan", true);
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

bool sgview_is_again_row(size_t index)
{
    return index == s_again_row;
}
