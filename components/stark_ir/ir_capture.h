/*
 * ir_capture.h — private to stark_ir: the capture ring between the IR task
 * and the UI (TASKS.md STARK-0303). Pure; the port guards it with a lock.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "ir_types.h"

#define IR_CAPTURE_SLOTS 4

typedef struct {
    ir_signal_t slot[IR_CAPTURE_SLOTS];
    uint8_t head, count;
    uint32_t dropped; /* captures lost because the ring was full */
} ir_capture_t;

void ir_capture_init(ir_capture_t *r);
/* Stores a copy; a full ring drops its oldest capture (counted). */
void ir_capture_push(ir_capture_t *r, const ir_signal_t *s);
/* Oldest capture; false when empty. */
bool ir_capture_take(ir_capture_t *r, ir_signal_t *out);
