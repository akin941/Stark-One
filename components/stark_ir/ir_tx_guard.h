/*
 * ir_tx_guard.h — private to stark_ir: the transmit gate (TASKS.md
 * STARK-0304, ADR-0019 §4, SECURITY_SCOPE §4). Pure; host-tested.
 *
 * Every transmission passes here first. The limits are constants, not
 * settings: nothing at run time can widen them.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "ir_types.h"
#include "stark_err.h"

#define IR_TX_MIN_US       50u
#define IR_TX_MAX_TOTAL_US 1000000u /* all repeats together: at most 1 s of signal */
#define IR_TX_MAX_REPEATS  3u       /* extra frames per confirmed transmission */
#define IR_TX_CARRIER_MIN  30000u
#define IR_TX_CARRIER_MAX  60000u
#define IR_TX_DUTY_PCT     33u

typedef struct {
    uint32_t signal_us; /* one frame's duration */
    uint32_t period_ms; /* start-to-start spacing of repeated frames */
    uint32_t total_ms;  /* everything, repeats included */
} ir_tx_plan_t;

/*
 * Checks s and repeats against every rule: 1..IR_MAX_DURATIONS durations,
 * an odd count (a frame ends on a mark), each >= IR_TX_MIN_US, a carrier in
 * range, repeats <= IR_TX_MAX_REPEATS, total signal time (each frame, the
 * repeats included) <= IR_TX_MAX_TOTAL_US. STARK_ERR_INVALID_ARG otherwise.
 */
stark_err_t ir_tx_check(const ir_signal_t *s, uint8_t repeats, ir_tx_plan_t *plan);

/* Rate limit: STARK_ERR_BUSY while fewer than min_interval_ms have passed
 * since the previous transmission ended (none yet: always OK). Wrap-safe. */
stark_err_t ir_tx_rate(bool have_last, uint32_t last_end_ms, uint32_t now_ms,
                       uint32_t min_interval_ms);

/* The protocol's frame period for repeats: NEC/NECX 108 ms, RC5/RC6 114 ms,
 * SIRC 45 ms, raw 100 ms plus the frame. */
uint32_t ir_tx_period_ms(ir_protocol_t p, uint32_t signal_us);
