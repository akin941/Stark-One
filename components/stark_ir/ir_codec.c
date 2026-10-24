/*
 * ir_codec.c — protocol dispatch (ir_codec.h). Pure.
 */
#include "ir_codec.h"
#include <stddef.h>
#include "ir_proto.h"

static const char *const k_names[IR_PROTO_COUNT] = {
    [IR_PROTO_UNKNOWN] = "RAW",   [IR_PROTO_NEC] = "NEC",       [IR_PROTO_NECX] = "NECX",
    [IR_PROTO_RC5] = "RC5",       [IR_PROTO_RC6] = "RC6",       [IR_PROTO_SIRC12] = "SIRC12",
    [IR_PROTO_SIRC15] = "SIRC15", [IR_PROTO_SIRC20] = "SIRC20",
};

const char *ir_protocol_name(ir_protocol_t p)
{
    return (unsigned)p < IR_PROTO_COUNT ? k_names[p] : "?";
}

stark_err_t ir_encode(const ir_frame_t *f, ir_timings_t *out, uint32_t *carrier_hz)
{
    if (f == NULL || out == NULL || carrier_hz == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    switch (f->protocol) {
        case IR_PROTO_NEC:
        case IR_PROTO_NECX:
            *carrier_hz = IR_NEC_CARRIER_HZ;
            return ir_nec_encode(f, out);
        case IR_PROTO_RC5:
            *carrier_hz = IR_RC5_CARRIER_HZ;
            return ir_rc5_encode(f, out);
        case IR_PROTO_RC6:
            *carrier_hz = IR_RC6_CARRIER_HZ;
            return ir_rc6_encode(f, out);
        case IR_PROTO_SIRC12:
        case IR_PROTO_SIRC15:
        case IR_PROTO_SIRC20:
            *carrier_hz = IR_SIRC_CARRIER_HZ;
            return ir_sirc_encode(f, out);
        case IR_PROTO_UNKNOWN:
        case IR_PROTO_COUNT:
            break;
    }
    return STARK_ERR_INVALID_ARG;
}

ir_protocol_t ir_decode(const ir_timings_t *t, ir_frame_t *out)
{
    if (t == NULL || out == NULL) {
        return IR_PROTO_UNKNOWN;
    }
    *out = (ir_frame_t){.protocol = IR_PROTO_UNKNOWN};
    /* Fixed order (TASKS.md STARK-0302): the protocols with a distinctive
     * leader first, RC5 — which has none — last. */
    if (ir_nec_decode(t, out) || ir_rc6_decode(t, out) || ir_sirc_decode(t, out) ||
        ir_rc5_decode(t, out)) {
        return out->protocol;
    }
    return IR_PROTO_UNKNOWN;
}
