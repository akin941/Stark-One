/*
 * subghz_rssi.c — RSSI conversion, text and the band scan (subghz_rssi.h). Pure.
 */
#include "subghz_rssi.h"
#include <stdio.h>

int16_t subghz_rssi_dbm_x2(uint8_t raw)
{
    int v = raw >= 128u ? (int)raw - 256 : (int)raw;
    return (int16_t)(v - SUBGHZ_RSSI_OFFSET_X2);
}

void subghz_dbm_text(char *out, size_t size, int16_t dbm_x2)
{
    unsigned mag = (unsigned)(dbm_x2 < 0 ? -(int)dbm_x2 : (int)dbm_x2);
    (void)snprintf(out, size, "%s%u%s", dbm_x2 < 0 ? "-" : "", mag / 2u,
                   (mag & 1u) != 0 ? ".5" : "");
}

void subghz_mhz_text(char *out, size_t size, uint32_t hz)
{
    (void)snprintf(out, size, "%u.%03u", (unsigned)(hz / 1000000u),
                   (unsigned)((hz / 1000u) % 1000u));
}

void subghz_scan_start(subghz_scan_t *s, const subghz_probe_t *probe)
{
    s->probe = *probe;
    s->state = SUBGHZ_SCAN_RUNNING;
    s->next = 0;
    s->peak_dbm_x2 = INT16_MIN;
    s->peak_hz = 0;
    s->err = STARK_OK;
}

static void fail(subghz_scan_t *s, stark_err_t err)
{
    s->state = SUBGHZ_SCAN_FAILED;
    s->err = err;
}

subghz_scan_state_t subghz_scan_step(subghz_scan_t *s)
{
    if (s->state != SUBGHZ_SCAN_RUNNING) {
        return s->state;
    }
    uint32_t hz = SUBGHZ_SCAN_FIRST_HZ + (uint32_t)s->next * SUBGHZ_SCAN_STEP_HZ;
    stark_err_t err = s->probe.tune(s->probe.ctx, hz);
    if (err != STARK_OK) {
        fail(s, err);
        return s->state;
    }
    s->probe.delay_ms(s->probe.ctx, SUBGHZ_SCAN_SETTLE_MS);
    int16_t best = INT16_MIN;
    for (unsigned i = 0; i < SUBGHZ_SCAN_READS; i++) {
        uint8_t raw;
        err = s->probe.read(s->probe.ctx, &raw);
        if (err != STARK_OK) {
            fail(s, err);
            return s->state;
        }
        int16_t level = subghz_rssi_dbm_x2(raw);
        if (level > best) {
            best = level;
        }
    }
    if (best > s->peak_dbm_x2) { /* strictly: the lowest channel keeps a tie */
        s->peak_dbm_x2 = best;
        s->peak_hz = hz;
    }
    if (++s->next == SUBGHZ_SCAN_CHANNELS) {
        s->state = SUBGHZ_SCAN_DONE;
    }
    return s->state;
}

uint8_t subghz_scan_percent(const subghz_scan_t *s)
{
    if (s->state == SUBGHZ_SCAN_DONE) {
        return 100u;
    }
    return (uint8_t)((unsigned)s->next * 100u / SUBGHZ_SCAN_CHANNELS);
}
