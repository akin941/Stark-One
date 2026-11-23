/*
 * subghz_txpolicy.c — the transmit policy (subghz_txpolicy.h). Pure.
 *
 * NOTE: ages are differences of uint32 ms, and an entry that is "in the future" (a clock that
 * stepped back) counts as age 0, so a misbehaving clock can only make the policy stricter.
 */
#include "subghz_txpolicy.h"
#include <string.h>

/* Milliseconds since `then`, 0 when `then` is ahead of `now`. Wrap-safe for spans < 2^31. */
static uint32_t age_ms(uint32_t now_ms, uint32_t then_ms)
{
    int32_t d = (int32_t)(now_ms - then_ms);
    return d < 0 ? 0u : (uint32_t)d;
}

const char *subghz_tx_verdict_name(subghz_tx_verdict_t v)
{
    switch (v) {
        case SUBGHZ_TX_OK:
            return "ok";
        case SUBGHZ_TX_REFUSE_BAND:
            return "band";
        case SUBGHZ_TX_REFUSE_LENGTH:
            return "length";
        case SUBGHZ_TX_REFUSE_DURATION:
            return "duration";
        case SUBGHZ_TX_REFUSE_BUDGET:
            return "budget";
        case SUBGHZ_TX_REFUSE_OFFTIME:
            return "offtime";
        case SUBGHZ_TX_REFUSE_FILE:
            return "file";
        case SUBGHZ_TX_REFUSE_NO_MODULE:
            return "no module";
        case SUBGHZ_TX_REFUSE_BUSY:
            return "busy";
        case SUBGHZ_TX_VERDICT_COUNT:
        default:
            return "?";
    }
}

uint32_t subghz_tx_ms(uint32_t span_us)
{
    uint32_t ms = span_us / 1000u + (span_us % 1000u != 0u ? 1u : 0u);
    return ms < 1u ? 1u : ms;
}

void subghz_txpolicy_init(subghz_txpolicy_t *p, uint32_t now_ms)
{
    memset(p, 0, sizeof *p);
    p->have_last = true;
    p->last_end_ms = now_ms;
    p->last_dur_ms = 2000u; /* as if a transmission of the longest kind had just ended */
}

void subghz_txpolicy_tick(subghz_txpolicy_t *p, uint32_t now_ms)
{
    size_t keep = 0;
    for (size_t i = 0; i < p->n; i++) {
        if (age_ms(now_ms, p->e[i].end_ms) < SUBGHZ_TX_WINDOW_MS) {
            p->e[keep++] = p->e[i];
        }
    }
    p->n = (uint8_t)keep;
    if (p->have_last && age_ms(now_ms, p->last_end_ms) >= SUBGHZ_TX_OFF_FACTOR * p->last_dur_ms) {
        p->have_last = false; /* the off-time is over: forget it before the clock can wrap */
    }
}

static uint32_t used_ms(const subghz_txpolicy_t *p)
{
    uint32_t used = 0;
    for (size_t i = 0; i < p->n; i++) {
        used += p->e[i].dur_ms;
    }
    return used;
}

uint32_t subghz_txpolicy_budget_left_ms(subghz_txpolicy_t *p, uint32_t now_ms)
{
    subghz_txpolicy_tick(p, now_ms);
    uint32_t used = used_ms(p);
    return used >= SUBGHZ_TX_BUDGET_MS ? 0u : SUBGHZ_TX_BUDGET_MS - used;
}

subghz_tx_verdict_t subghz_txpolicy_check(subghz_txpolicy_t *p, uint32_t now_ms,
                                          const subghz_tx_plan_t *plan, subghz_tx_info_t *info)
{
    uint32_t left = subghz_txpolicy_budget_left_ms(p, now_ms);
    uint32_t wait = 0;
    if (p->have_last) {
        uint32_t off = SUBGHZ_TX_OFF_FACTOR * p->last_dur_ms;
        uint32_t since = age_ms(now_ms, p->last_end_ms);
        wait = since < off ? off - since : 0u;
    }
    uint32_t ms = subghz_tx_ms(plan->span_us);
    if (info != NULL) {
        info->duration_ms = ms;
        info->budget_left_ms = left;
        info->wait_ms = wait;
        info->hash = 0;
    }
    if (plan->freq_hz < SUBGHZ_BAND_MIN_HZ || plan->freq_hz > SUBGHZ_BAND_MAX_HZ) {
        return SUBGHZ_TX_REFUSE_BAND;
    }
    if (plan->symbols < 1u || plan->symbols > SUBGHZ_TX_MAX_SYMBOLS || plan->span_us == 0u) {
        return SUBGHZ_TX_REFUSE_LENGTH;
    }
    if (plan->span_us > SUBGHZ_TX_MAX_US) {
        return SUBGHZ_TX_REFUSE_DURATION;
    }
    if (ms > left) {
        return SUBGHZ_TX_REFUSE_BUDGET;
    }
    if (wait > 0u) {
        return SUBGHZ_TX_REFUSE_OFFTIME;
    }
    return SUBGHZ_TX_OK;
}

void subghz_txpolicy_record(subghz_txpolicy_t *p, uint32_t start_ms, uint32_t end_ms,
                            uint32_t dur_ms)
{
    uint32_t planned_end = start_ms + dur_ms;
    uint32_t end = age_ms(end_ms, planned_end) > 0u ? end_ms : planned_end;
    if (p->n == SUBGHZ_TX_LEDGER) {
        /* NOTE: full (more than 192 transmissions in one hour: only short ones can): the two
         * oldest become one that is charged the sum and lives as long as the later of them, so
         * the window can only ever look fuller than it is */
        p->e[1].dur_ms += p->e[0].dur_ms;
        memmove(&p->e[0], &p->e[1], (SUBGHZ_TX_LEDGER - 1u) * sizeof p->e[0]);
        p->n--;
    }
    p->e[p->n].end_ms = end;
    p->e[p->n].dur_ms = dur_ms;
    p->n++;
    p->have_last = true;
    p->last_end_ms = end;
    p->last_dur_ms = dur_ms;
}
