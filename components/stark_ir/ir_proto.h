/*
 * ir_proto.h — private to stark_ir: the per-protocol encoders and decoders
 * the dispatcher (ir_codec.c) calls. Each decoder answers only for its own
 * protocol and leaves *out untouched when the timings are not its own.
 */
#pragma once

#include <stdbool.h>
#include "ir_types.h"
#include "stark_err.h"

/* NEC and NEC-extended, 38 kHz (ir_nec.c). */
#define IR_NEC_CARRIER_HZ 38000u
stark_err_t ir_nec_encode(const ir_frame_t *f, ir_timings_t *out);
bool ir_nec_decode(const ir_timings_t *t, ir_frame_t *out);

/* Philips RC5, 36 kHz (ir_rc5.c). */
#define IR_RC5_CARRIER_HZ 36000u
stark_err_t ir_rc5_encode(const ir_frame_t *f, ir_timings_t *out);
bool ir_rc5_decode(const ir_timings_t *t, ir_frame_t *out);

/* Philips RC6 mode 0, 36 kHz (ir_rc6.c). */
#define IR_RC6_CARRIER_HZ 36000u
stark_err_t ir_rc6_encode(const ir_frame_t *f, ir_timings_t *out);
bool ir_rc6_decode(const ir_timings_t *t, ir_frame_t *out);

/* Sony SIRC 12/15/20-bit, 40 kHz (ir_sirc.c). */
#define IR_SIRC_CARRIER_HZ 40000u
stark_err_t ir_sirc_encode(const ir_frame_t *f, ir_timings_t *out);
bool ir_sirc_decode(const ir_timings_t *t, ir_frame_t *out);
