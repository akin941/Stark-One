/*
 * ui_port_subghz.c — host UI test port: settable stark_subghz backend
 * (ui_port_subghz.h). Starts take effect at once; the test supplies the readings.
 */
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
