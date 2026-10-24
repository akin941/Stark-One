/*
 * ir_timing.c — duration matching and building (ir_timing.h). Pure.
 */
#include "ir_timing.h"

bool ir_match(uint32_t measured_us, uint32_t nominal_us, uint8_t tol_pct)
{
    uint32_t tol = nominal_us * tol_pct / 100u;
    if (tol < IR_TOL_FLOOR_US) {
        tol = IR_TOL_FLOOR_US;
    }
    uint32_t lo = nominal_us > tol ? nominal_us - tol : 0;
    return measured_us >= lo && measured_us <= nominal_us + tol;
}

bool ir_push(ir_timings_t *t, uint32_t us)
{
    if (t->n >= IR_MAX_DURATIONS || us == 0 || us > UINT16_MAX) {
        return false;
    }
    t->us[t->n++] = (uint16_t)us;
    return true;
}
