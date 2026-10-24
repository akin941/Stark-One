/*
 * ir_nec.c — NEC and NEC-extended (TASKS.md STARK-0301). Pure.
 *
 * Leader mark 9000 us, space 4500; 32 bits LSB first, each a 560 us mark
 * then a 560 (0) or 1690 (1) us space; a final 560 us mark. Byte order:
 * address, inverted address, command, inverted command. When the address
 * bytes are not complements the frame is NEC-extended (16-bit address);
 * the command bytes must always be complements. The repeat code is
 * 9000 / 2250 / 560.
 */
#include "ir_codec.h"
#include "ir_proto.h"
#include "ir_timing.h"

#define LEAD_MARK    9000u
#define LEAD_SPACE   4500u
#define REPEAT_SPACE 2250u
#define BIT_MARK     560u
#define ZERO_SPACE   560u
#define ONE_SPACE    1690u
#define FULL_LEN     (2u + 64u + 1u)
#define REPEAT_LEN   3u

stark_err_t ir_nec_encode(const ir_frame_t *f, ir_timings_t *out)
{
    out->n = 0;
    if (f->repeat) {
        (void)ir_push(out, LEAD_MARK);
        (void)ir_push(out, REPEAT_SPACE);
        (void)ir_push(out, BIT_MARK);
        return STARK_OK;
    }
    if (f->command > 0xFFu || (f->protocol == IR_PROTO_NEC && f->address > 0xFFu)) {
        return STARK_ERR_INVALID_ARG;
    }
    uint32_t a = f->address;
    uint32_t addr = f->protocol == IR_PROTO_NEC ? (a | ((~a & 0xFFu) << 8)) : a;
    uint32_t word = addr | ((uint32_t)f->command << 16) | ((~(uint32_t)f->command & 0xFFu) << 24);
    (void)ir_push(out, LEAD_MARK);
    (void)ir_push(out, LEAD_SPACE);
    for (int i = 0; i < 32; i++) {
        (void)ir_push(out, BIT_MARK);
        (void)ir_push(out, (word >> i) & 1u ? ONE_SPACE : ZERO_SPACE);
    }
    (void)ir_push(out, BIT_MARK);
    return STARK_OK;
}

bool ir_nec_decode(const ir_timings_t *t, ir_frame_t *out)
{
    const uint16_t *d = t->us;
    if (t->n == REPEAT_LEN && ir_match(d[0], LEAD_MARK, IR_TOL_PCT) &&
        ir_match(d[1], REPEAT_SPACE, IR_TOL_PCT) && ir_match(d[2], BIT_MARK, IR_TOL_PCT)) {
        *out = (ir_frame_t){.protocol = IR_PROTO_NEC, .repeat = true};
        return true;
    }
    if (t->n != FULL_LEN || !ir_match(d[0], LEAD_MARK, IR_TOL_PCT) ||
        !ir_match(d[1], LEAD_SPACE, IR_TOL_PCT) ||
        !ir_match(d[FULL_LEN - 1], BIT_MARK, IR_TOL_PCT)) {
        return false;
    }
    uint32_t word = 0;
    for (uint32_t i = 0; i < 32; i++) {
        uint16_t mark = d[2 + 2 * i];
        uint16_t space = d[3 + 2 * i];
        if (!ir_match(mark, BIT_MARK, IR_TOL_PCT)) {
            return false;
        }
        if (ir_match(space, ONE_SPACE, IR_TOL_PCT)) {
            word |= 1u << i;
        } else if (!ir_match(space, ZERO_SPACE, IR_TOL_PCT)) {
            return false;
        }
    }
    uint32_t a = word & 0xFFu, na = (word >> 8) & 0xFFu;
    uint32_t c = (word >> 16) & 0xFFu, nc = (word >> 24) & 0xFFu;
    if ((c ^ nc) != 0xFFu) {
        return false; /* the command check is part of both variants */
    }
    bool standard = (a ^ na) == 0xFFu;
    *out = (ir_frame_t){
        .protocol = standard ? IR_PROTO_NEC : IR_PROTO_NECX,
        .address = (uint16_t)(standard ? a : (a | (na << 8))),
        .command = (uint16_t)c,
    };
    return true;
}
