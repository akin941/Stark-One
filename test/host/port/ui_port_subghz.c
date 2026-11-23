/*
 * ui_port_subghz.c — host UI test port: settable stark_subghz backend
 * (ui_port_subghz.h). Starts take effect at once; the test supplies the readings.
 */
#include <stdio.h>
#include <string.h>
#include "stark_event.h"
#include "subghz_rssi.h"
#include "ui_port_subghz.h"

static bool s_ready;
static bool s_rx;
static stark_subghz_status_t s_st;
static unsigned s_meters, s_scans, s_stops;
static uint32_t s_hz;
static stark_err_t s_refuse;
static subghz_cap_t s_cap;
static unsigned s_cap_starts, s_cap_finishes;
static subghz_mod_t s_cap_mod;
static uint32_t s_cap_hz;
static subghz_tx_verdict_t s_tx_verdict;
static uint32_t s_tx_budget = SUBGHZ_TX_BUDGET_MS;
static uint32_t s_tx_wait;
static unsigned s_tx_checks, s_tx_starts;
static uint32_t s_tx_hash;
static char s_tx_name[16];

static void publish(int32_t what)
{
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_SUBGHZ;
    e.system.arg = what;
    (void)stark_event_publish(&e);
}

void ui_port_subghz_reset(void)
{
    s_ready = s_rx = false;
    memset(&s_st, 0, sizeof s_st);
    s_meters = s_scans = s_stops = 0;
    s_hz = 0;
    s_refuse = STARK_OK;
    memset(&s_cap, 0, sizeof s_cap);
    s_cap_starts = s_cap_finishes = 0;
    s_cap_mod = SUBGHZ_MOD_OOK;
    s_cap_hz = 0;
    s_tx_verdict = SUBGHZ_TX_OK;
    s_tx_budget = SUBGHZ_TX_BUDGET_MS;
    s_tx_wait = 0;
    s_tx_checks = s_tx_starts = 0;
    s_tx_hash = 0;
    s_tx_name[0] = '\0';
}

void ui_port_subghz_tx_policy(subghz_tx_verdict_t verdict, uint32_t budget_left_ms,
                              uint32_t wait_ms)
{
    s_tx_verdict = verdict;
    s_tx_budget = budget_left_ms;
    s_tx_wait = wait_ms;
}

unsigned ui_port_subghz_tx_checks(void)
{
    return s_tx_checks;
}

unsigned ui_port_subghz_tx_starts(void)
{
    return s_tx_starts;
}

uint32_t ui_port_subghz_tx_hash(void)
{
    return s_tx_hash;
}

const char *ui_port_subghz_tx_name(void)
{
    return s_tx_name;
}

void ui_port_subghz_tx_finish(int evt)
{
    s_st.tx_done = true;
    s_rx = false;
    publish(evt);
}

void ui_port_subghz_capture_feed(const subghz_sym_t *s, size_t n)
{
    subghz_cap_feed(&s_cap, s, n);
    s_st.symbols = s_cap.n;
    s_st.span_us = s_cap.total_us;
    publish(STARK_SUBGHZ_EVT_CAPTURE);
}

void ui_port_subghz_capture_end(void)
{
    s_rx = false;
    s_st.capture_done = true;
    publish(STARK_SUBGHZ_EVT_CAPTURE_DONE);
}

unsigned ui_port_subghz_capture_starts(void)
{
    return s_cap_starts;
}

unsigned ui_port_subghz_capture_finishes(void)
{
    return s_cap_finishes;
}

subghz_mod_t ui_port_subghz_capture_mod(void)
{
    return s_cap_mod;
}

uint32_t ui_port_subghz_capture_hz(void)
{
    return s_cap_hz;
}

void ui_port_subghz_set_ready(bool ready)
{
    s_ready = ready;
    if (!ready) {
        s_rx = false;
        memset(&s_st, 0, sizeof s_st);
    }
    publish(STARK_SUBGHZ_EVT_MODULE);
}

void ui_port_subghz_set_level(int16_t dbm_x2)
{
    s_st.valid = true;
    s_st.dbm_x2 = dbm_x2;
    publish(STARK_SUBGHZ_EVT_RSSI);
}

void ui_port_subghz_finish_scan(uint32_t hz, int16_t dbm_x2)
{
    s_rx = false;
    s_st.scan_done = true;
    s_st.valid = true;
    s_st.percent = 100;
    s_st.hz = hz;
    s_st.dbm_x2 = dbm_x2;
    publish(STARK_SUBGHZ_EVT_SCAN_DONE);
}

void ui_port_subghz_refuse(stark_err_t err)
{
    s_refuse = err;
}

bool ui_port_subghz_receiving(void)
{
    return s_rx;
}

uint32_t ui_port_subghz_meter_hz(void)
{
    return s_hz;
}

unsigned ui_port_subghz_meter_starts(void)
{
    return s_meters;
}

unsigned ui_port_subghz_scan_starts(void)
{
    return s_scans;
}

unsigned ui_port_subghz_stops(void)
{
    return s_stops;
}

stark_err_t stark_subghz_init(void)
{
    return STARK_OK;
}

bool stark_subghz_ready(void)
{
    return s_ready;
}

stark_err_t stark_subghz_meter_start(uint32_t hz)
{
    if (!s_ready || s_refuse != STARK_OK) {
        return s_ready ? s_refuse : STARK_ERR_STATE;
    }
    if (hz < SUBGHZ_BAND_MIN_HZ || hz > SUBGHZ_BAND_MAX_HZ) {
        return STARK_ERR_INVALID_ARG;
    }
    memset(&s_st, 0, sizeof s_st);
    s_st.mode = STARK_SUBGHZ_METER;
    s_st.hz = hz;
    s_hz = hz;
    s_rx = true;
    s_meters++;
    return STARK_OK;
}

stark_err_t stark_subghz_scan_start(void)
{
    if (!s_ready || s_refuse != STARK_OK) {
        return s_ready ? s_refuse : STARK_ERR_STATE;
    }
    memset(&s_st, 0, sizeof s_st);
    s_st.mode = STARK_SUBGHZ_SCAN;
    s_rx = true;
    s_scans++;
    return STARK_OK;
}

stark_err_t stark_subghz_capture_start(subghz_mod_t mod, uint32_t hz, subghz_sym_t *buf, size_t cap)
{
    if (!s_ready || s_refuse != STARK_OK) {
        return s_ready ? s_refuse : STARK_ERR_STATE;
    }
    if (hz < SUBGHZ_BAND_MIN_HZ || hz > SUBGHZ_BAND_MAX_HZ || buf == NULL || cap == 0) {
        return STARK_ERR_INVALID_ARG;
    }
    memset(&s_st, 0, sizeof s_st);
    s_st.mode = STARK_SUBGHZ_CAPTURE;
    s_st.hz = hz;
    subghz_cap_init(&s_cap, buf, cap);
    s_cap_mod = mod;
    s_cap_hz = hz;
    s_rx = true;
    s_cap_starts++;
    return STARK_OK;
}

void stark_subghz_capture_finish(void)
{
    s_cap_finishes++; /* the test ends the capture when it wants: the real service answers later */
}

void stark_subghz_stop(void)
{
    memset(&s_st, 0, sizeof s_st);
    s_rx = false;
    s_stops++;
}

void stark_subghz_status(stark_subghz_status_t *out)
{
    *out = s_st;
}

subghz_tx_verdict_t stark_subghz_tx_check(const subghz_raw_t *raw, const char *name,
                                          subghz_tx_info_t *info)
{
    (void)name;
    s_tx_checks++;
    subghz_tx_info_t local = {subghz_tx_ms(subghz_raw_duration_us(raw)), s_tx_budget, s_tx_wait,
                              subghz_raw_hash(raw->sym, raw->n)};
    if (info != NULL) {
        *info = local;
    }
    return s_ready ? s_tx_verdict : SUBGHZ_TX_REFUSE_NO_MODULE;
}

subghz_tx_verdict_t stark_subghz_tx_start(const subghz_raw_t *raw, const char *name, uint32_t hash,
                                          subghz_tx_info_t *info)
{
    subghz_tx_verdict_t v = stark_subghz_tx_check(raw, name, info);
    s_tx_checks--; /* a start is not a check */
    if (v == SUBGHZ_TX_OK && subghz_raw_hash(raw->sym, raw->n) != hash) {
        v = SUBGHZ_TX_REFUSE_FILE;
    }
    if (v == SUBGHZ_TX_OK) {
        memset(&s_st, 0, sizeof s_st);
        s_st.mode = STARK_SUBGHZ_TX;
        s_rx = true; /* the radio is out of IDLE until the test ends the transmission */
        s_tx_starts++;
        s_tx_hash = hash;
        (void)snprintf(s_tx_name, sizeof s_tx_name, "%s", name);
    }
    return v;
}
