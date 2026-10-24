/*
 * ir_sirc.c — Sony SIRC, 12/15/20-bit (TASKS.md STARK-0302). Pure.
 *
 * 40 kHz. Start mark 2400 us, then each bit a 600 us space and a 1200 (1)
 * or 600 (0) us mark, LSB first: 7 command bits, then 5 (12-bit), 8
 * (15-bit) or 5 + 8 (20-bit) address bits. The variant is the bit count;
 * a 20-bit frame's 13 address bits hold the device and its extension.
 */
#include "ir_codec.h"
#include "ir_proto.h"
#include "ir_timing.h"

#define START_MARK 2400u
#define SPACE      600u
#define ONE_MARK   1200u
#define ZERO_MARK  600u

static unsigned bits_of(ir_protocol_t p)
{
    return p == IR_PROTO_SIRC12 ? 12u : (p == IR_PROTO_SIRC15 ? 15u : 20u);
}

stark_err_t ir_sirc_encode(const ir_frame_t *f, ir_timings_t *out)
{
    unsigned bits = bits_of(f->protocol);
    uint32_t addr_max = (1u << (bits - 7u)) - 1u;
    if (f->command > 0x7Fu || f->address > addr_max) {
        return STARK_ERR_INVALID_ARG;
    }
    uint32_t word = f->command | ((uint32_t)f->address << 7);
    out->n = 0;
    (void)ir_push(out, START_MARK);
    for (unsigned i = 0; i < bits; i++) {
        (void)ir_push(out, SPACE);
        (void)ir_push(out, (word >> i) & 1u ? ONE_MARK : ZERO_MARK);
    }
    return STARK_OK;
}

bool ir_sirc_decode(const ir_timings_t *t, ir_frame_t *out)
{
    ir_protocol_t p;
    switch (t->n) {
        case 1 + 2 * 12:
            p = IR_PROTO_SIRC12;
            break;
        case 1 + 2 * 15:
            p = IR_PROTO_SIRC15;
            break;
        case 1 + 2 * 20:
            p = IR_PROTO_SIRC20;
            break;
        default:
            return false;
    }
    if (!ir_match(t->us[0], START_MARK, IR_TOL_PCT)) {
        return false;
    }
    unsigned bits = bits_of(p);
    uint32_t word = 0;
    for (unsigned i = 0; i < bits; i++) {
        uint16_t space = t->us[1 + 2 * i];
        uint16_t mark = t->us[2 + 2 * i];
        if (!ir_match(space, SPACE, IR_TOL_PCT)) {
            return false;
        }
        if (ir_match(mark, ONE_MARK, IR_TOL_PCT)) {
            word |= 1u << i;
        } else if (!ir_match(mark, ZERO_MARK, IR_TOL_PCT)) {
            return false;
        }
    }
    *out = (ir_frame_t){
        .protocol = p, .command = (uint16_t)(word & 0x7Fu), .address = (uint16_t)(word >> 7)};
    return true;
}
