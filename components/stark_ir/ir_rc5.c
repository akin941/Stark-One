/*
 * ir_rc5.c — Philips RC5 (TASKS.md STARK-0302). Pure.
 *
 * 36 kHz, bi-phase, unit 889 us, 14 bits MSB first: S1 = 1, S2 = the
 * inverted bit 6 of the command (RC5's extended field), toggle, 5 address
 * bits, 6 command bits. A one is space-then-mark, so the frame's first half
 * (S1's space) is invisible: a capture starts with S1's mark.
 */
#include "ir_biphase.h"
#include "ir_codec.h"
#include "ir_proto.h"

#define UNIT 889u
#define BITS 14u

stark_err_t ir_rc5_encode(const ir_frame_t *f, ir_timings_t *out)
{
    if (f->address > 0x1Fu || f->command > 0x7Fu || f->toggle > 1u) {
        return STARK_ERR_INVALID_ARG;
    }
    uint32_t word = (1u << 13) | ((f->command & 0x40u) ? 0u : (1u << 12)) |
                    ((uint32_t)f->toggle << 11) | ((uint32_t)f->address << 6) |
                    (f->command & 0x3Fu);
    uint8_t halves[2 * BITS];
    for (uint32_t i = 0; i < BITS; i++) {
        uint32_t bit = (word >> (BITS - 1 - i)) & 1u;
        halves[2 * i] = (uint8_t)(bit ? 0 : 1); /* one: space, mark */
        halves[2 * i + 1] = (uint8_t)(bit ? 1 : 0);
    }
    out->n = 0;
    ir_biphase_emit(halves, 2 * BITS, UNIT, out);
    return STARK_OK;
}

bool ir_rc5_decode(const ir_timings_t *t, ir_frame_t *out)
{
    uint8_t halves[IR_BIPHASE_MAX_HALVES];
    size_t n = 0;
    halves[0] = 0; /* S1's invisible space */
    if (t->n == 0 || !ir_biphase_expand(t, 0, UNIT, 2, 1, halves + 1, 2 * BITS, &n)) {
        return false;
    }
    n += 1;
    if (n == 2 * BITS - 1) {
        halves[n++] = 0; /* a final zero bit ends on an invisible space */
    }
    if (n != 2 * BITS) {
        return false;
    }
    uint32_t word = 0;
    for (uint32_t i = 0; i < BITS; i++) {
        uint8_t a = halves[2 * i], b = halves[2 * i + 1];
        if (a == b) {
            return false; /* not a bi-phase bit */
        }
        word = (word << 1) | b;
    }
    /* S1 is 1 by construction: the capture's first duration is a mark,
     * which follows S1's invisible space. */
    *out = (ir_frame_t){
        .protocol = IR_PROTO_RC5,
        .toggle = (uint8_t)((word >> 11) & 1u),
        .address = (uint16_t)((word >> 6) & 0x1Fu),
        .command = (uint16_t)((word & 0x3Fu) | (((word >> 12) & 1u) ? 0u : 0x40u)),
    };
    return true;
}
