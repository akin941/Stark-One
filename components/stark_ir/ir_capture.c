/*
 * ir_capture.c — the capture ring (ir_capture.h). Pure.
 */
#include "ir_capture.h"

void ir_capture_init(ir_capture_t *r)
{
    r->head = 0;
    r->count = 0;
    r->dropped = 0;
}

void ir_capture_push(ir_capture_t *r, const ir_signal_t *s)
{
    if (r->count == IR_CAPTURE_SLOTS) {
        r->head = (uint8_t)((r->head + 1u) % IR_CAPTURE_SLOTS);
        r->count--;
        r->dropped++;
    }
    r->slot[(r->head + r->count) % IR_CAPTURE_SLOTS] = *s;
    r->count++;
}

bool ir_capture_take(ir_capture_t *r, ir_signal_t *out)
{
    if (r->count == 0) {
        return false;
    }
    *out = r->slot[r->head];
    r->head = (uint8_t)((r->head + 1u) % IR_CAPTURE_SLOTS);
    r->count--;
    return true;
}
