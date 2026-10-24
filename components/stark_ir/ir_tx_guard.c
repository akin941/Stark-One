/*
 * ir_tx_guard.c — the transmit gate (ir_tx_guard.h). Pure.
 */
#include "ir_tx_guard.h"
#include <stddef.h>

uint32_t ir_tx_period_ms(ir_protocol_t p, uint32_t signal_us)
{
    switch (p) {
        case IR_PROTO_NEC:
        case IR_PROTO_NECX:
            return 108u;
        case IR_PROTO_RC5:
        case IR_PROTO_RC6:
            return 114u;
        case IR_PROTO_SIRC12:
        case IR_PROTO_SIRC15:
        case IR_PROTO_SIRC20:
            return 45u;
        case IR_PROTO_UNKNOWN:
        case IR_PROTO_COUNT:
            break;
    }
    return (signal_us + 999u) / 1000u + 100u; /* raw: the frame, then a 100 ms gap */
}

stark_err_t ir_tx_check(const ir_signal_t *s, uint8_t repeats, ir_tx_plan_t *plan)
{
    if (s == NULL || plan == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    const ir_timings_t *t = &s->timings;
    if (t->n == 0 || t->n > IR_MAX_DURATIONS || t->n % 2 == 0 || repeats > IR_TX_MAX_REPEATS ||
        s->carrier_hz < IR_TX_CARRIER_MIN || s->carrier_hz > IR_TX_CARRIER_MAX) {
        return STARK_ERR_INVALID_ARG;
    }
    uint32_t signal = 0;
    for (uint16_t i = 0; i < t->n; i++) {
        if (t->us[i] < IR_TX_MIN_US) {
            return STARK_ERR_INVALID_ARG;
        }
        signal += t->us[i];
    }
    uint32_t frames = (uint32_t)repeats + 1u;
    if ((uint64_t)signal * frames > IR_TX_MAX_TOTAL_US) {
        return STARK_ERR_INVALID_ARG;
    }
    uint32_t period = ir_tx_period_ms(s->frame.protocol, signal);
    uint32_t frame_ms = (signal + 999u) / 1000u;
    if (period < frame_ms) {
        period = frame_ms; /* a long frame: back to back */
    }
    *plan = (ir_tx_plan_t){
        .signal_us = signal,
        .period_ms = period,
        .total_ms = period * repeats + frame_ms,
    };
    return STARK_OK;
}

stark_err_t ir_tx_rate(bool have_last, uint32_t last_end_ms, uint32_t now_ms,
                       uint32_t min_interval_ms)
{
    if (!have_last) {
        return STARK_OK;
    }
    return now_ms - last_end_ms >= min_interval_ms ? STARK_OK : STARK_ERR_BUSY;
}
