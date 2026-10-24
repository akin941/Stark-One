/*
 * ir_rc6.c — Philips RC6 mode 0 (TASKS.md STARK-0302). Pure.
 *
 * 36 kHz, unit 444 us. Leader mark 2666 / space 889, then bi-phase with RC6
 * polarity (a one is mark-then-space): start bit 1, mode 000, the trailer
 * (toggle) bit at double length, 8 address and 8 command bits MSB first.
 * Other modes decode as raw.
 *
 * Decoding tries the nominal unit first (it absorbs independent per-pulse
 * jitter), then a unit estimated from the leader — for a frame whose timing
 * is uniformly fast or slow — but only when the leader's mark and space
 * agree on that estimate, so one bad pulse cannot rescale the frame.
 */
#include "ir_biphase.h"
#include "ir_codec.h"
#include "ir_proto.h"
#include "ir_timing.h"

#define UNIT       444u
#define LEAD_MARK  2666u
#define LEAD_SPACE 889u
#define HALVES     44u /* start 2 + mode 6 + trailer 4 + 16 bits x 2 */

stark_err_t ir_rc6_encode(const ir_frame_t *f, ir_timings_t *out)
{
    if (f->address > 0xFFu || f->command > 0xFFu || f->toggle > 1u) {
        return STARK_ERR_INVALID_ARG;
    }
    uint8_t h[HALVES];
    size_t n = 0;
    h[n++] = 1; /* start bit 1 */
    h[n++] = 0;
    for (int i = 0; i < 3; i++) { /* mode 000 */
        h[n++] = 0;
        h[n++] = 1;
    }
    uint8_t first = f->toggle ? 1u : 0u; /* trailer: double length */
    h[n++] = first;
    h[n++] = first;
    h[n++] = (uint8_t)!first;
    h[n++] = (uint8_t)!first;
    uint32_t data = ((uint32_t)f->address << 8) | f->command;
    for (int i = 15; i >= 0; i--) {
        uint8_t bit = (uint8_t)((data >> i) & 1u);
        h[n++] = bit;
        h[n++] = (uint8_t)!bit;
    }
    out->n = 0;
    (void)ir_push(out, LEAD_MARK);
    (void)ir_push(out, LEAD_SPACE);
    ir_biphase_emit(h, n, UNIT, out);
    return STARK_OK;
}

static bool parse(const ir_timings_t *t, uint32_t unit, ir_frame_t *out)
{
    uint8_t h[IR_BIPHASE_MAX_HALVES];
    size_t n = 0;
    if (!ir_biphase_expand(t, 2, unit, 3, 1, h, HALVES, &n)) {
        return false;
    }
    if (n == HALVES - 1) {
        h[n++] = 0; /* a final one bit ends on an invisible space */
    }
    if (n != HALVES || h[0] != 1 || h[1] != 0) {
        return false;
    }
    for (size_t i = 2; i < 8; i += 2) {
        if (h[i] != 0 || h[i + 1] != 1) {
            return false; /* mode 000 only */
        }
    }
    if (h[8] != h[9] || h[10] != h[11] || h[8] == h[10]) {
        return false; /* trailer must be a double-length bi-phase bit */
    }
    uint32_t data = 0;
    for (size_t i = 12; i < HALVES; i += 2) {
        if (h[i] == h[i + 1]) {
            return false;
        }
        data = (data << 1) | h[i];
    }
    *out = (ir_frame_t){
        .protocol = IR_PROTO_RC6,
        .toggle = h[8],
        .address = (uint16_t)(data >> 8),
        .command = (uint16_t)(data & 0xFFu),
    };
    return true;
}

bool ir_rc6_decode(const ir_timings_t *t, ir_frame_t *out)
{
    if (t->n < 3) {
        return false;
    }
    if (ir_match(t->us[0], LEAD_MARK, IR_TOL_PCT) && ir_match(t->us[1], LEAD_SPACE, IR_TOL_PCT) &&
        parse(t, UNIT, out)) {
        return true;
    }
    /* Uniform scaling: both leader parts must point at the same unit. */
    uint32_t from_mark = t->us[0] / 6u;
    uint32_t from_space = t->us[1] / 2u;
    uint32_t unit = (t->us[0] + t->us[1]) / 8u;
    uint32_t diff = from_mark > from_space ? from_mark - from_space : from_space - from_mark;
    if (diff * 10u > unit || !ir_match(unit, UNIT, IR_TOL_PCT)) {
        return false;
    }
    return parse(t, unit, out);
}
