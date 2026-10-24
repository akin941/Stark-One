/*
 * ir_types.h — the protocol-independent IR model shared by every part of
 * stark_ir (TASKS.md V0.3 "Common definitions"). Pure: no ESP-IDF.
 *
 * A capture or a transmission is a list of durations in microseconds,
 * alternating mark (carrier on / receiver active) and space, starting with
 * a mark. Decoding turns timings into a frame; encoding the reverse.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IR_MAX_DURATIONS 256 /* marks and spaces in one capture or transmission */

typedef enum {
    IR_PROTO_UNKNOWN = 0, /* raw timings only */
    IR_PROTO_NEC,
    IR_PROTO_NECX, /* NEC with a 16-bit address */
    IR_PROTO_RC5,
    IR_PROTO_RC6, /* mode 0 */
    IR_PROTO_SIRC12,
    IR_PROTO_SIRC15,
    IR_PROTO_SIRC20,
    IR_PROTO_COUNT
} ir_protocol_t;

typedef struct {
    uint16_t n;                    /* durations used */
    uint16_t us[IR_MAX_DURATIONS]; /* mark, space, mark, ... */
} ir_timings_t;

typedef struct {
    ir_protocol_t protocol;
    uint16_t address; /* NEC 8, NECX 16, RC5 5, RC6 8, SIRC 5 / 8 / 13 bits */
    uint16_t command; /* NEC/NECX 8, RC5 7 (incl. the field bit), RC6 8, SIRC 7 */
    bool repeat;      /* NEC/NECX repeat code */
    uint8_t toggle;   /* RC5/RC6 toggle bit */
} ir_frame_t;

typedef struct {
    ir_timings_t timings;
    uint32_t carrier_hz;
    ir_frame_t frame;
} ir_signal_t;
