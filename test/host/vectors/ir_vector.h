/*
 * ir_vector.h — one IR codec test vector (scripts/gen_ir_vectors.py).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "ir_types.h"

typedef struct {
    const char *label;
    const uint16_t *timings;
    uint16_t n;
    ir_protocol_t expect; /* IR_PROTO_UNKNOWN: must be rejected */
    uint16_t address;
    uint16_t command;
    bool repeat;
    uint8_t toggle;
} ir_vector_t;
