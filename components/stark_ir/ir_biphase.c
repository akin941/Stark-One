/*
 * ir_biphase.c — bi-phase helpers (ir_biphase.h). Pure.
 */
#include "ir_biphase.h"
#include "ir_codec.h"
#include "ir_timing.h"

bool ir_biphase_expand(const ir_timings_t *t, size_t from, uint32_t unit_us, unsigned max_k,
                       uint8_t first_level, uint8_t *halves, size_t cap, size_t *count)
{
    size_t n = 0;
    uint8_t level = first_level;
    for (size_t i = from; i < t->n; i++) {
        uint32_t d = t->us[i];
        uint32_t k = (d + unit_us / 2) / unit_us;
        if (k < 1 || k > max_k || !ir_match(d, k * unit_us, IR_TOL_PCT) || n + k > cap) {
            return false;
        }
        for (uint32_t j = 0; j < k; j++) {
            halves[n++] = level;
        }
        level ^= 1u;
    }
    *count = n;
    return true;
}

void ir_biphase_emit(const uint8_t *halves, size_t count, uint32_t unit_us, ir_timings_t *out)
{
    size_t i = 0;
    while (i < count && halves[i] == 0) {
        i++; /* leading space: nothing to emit */
    }
    while (i < count) {
        size_t run = 1;
        while (i + run < count && halves[i + run] == halves[i]) {
            run++;
        }
        bool trailing_space = halves[i] == 0 && i + run == count;
        if (!trailing_space) {
            (void)ir_push(out, (uint32_t)run * unit_us);
        }
        i += run;
    }
}
