/*
 * storage_core.c — the mount state machine (storage_core.h). Pure.
 */
#include "storage_core.h"

static bool is_due(const storage_core_t *c, uint32_t now_ms)
{
    return c->due_now || (int32_t)(now_ms - c->due_ms) >= 0;
}

static void schedule(storage_core_t *c, uint32_t now_ms, uint32_t after_ms)
{
    c->due_now = false;
    c->due_ms = now_ms + after_ms;
}

/* Moves to `next`; reports a change, or the very first state ever reached
 * (the boot's "no card" must be announced even though ABSENT is the start). */
static bool enter(storage_core_t *c, stark_storage_state_t next)
{
    bool changed = next != c->state || !c->announced;
    c->state = next;
    c->announced = true;
    return changed;
}

void storage_core_init(storage_core_t *c, bool fitted, storage_timing_t timing, uint32_t now_ms)
{
    c->state = fitted ? STARK_STORAGE_ABSENT : STARK_STORAGE_NOT_FITTED;
    c->timing = timing;
    c->due_ms = now_ms;
    c->due_now = fitted;
    c->announced = !fitted;
    c->generation = 0;
}

static storage_step_t on_tick(storage_core_t *c, uint32_t now_ms)
{
    storage_step_t step = {STORAGE_ACT_NONE, false};
    if (!is_due(c, now_ms)) {
        return step;
    }
    switch (c->state) {
        case STARK_STORAGE_ABSENT:
        case STARK_STORAGE_UNUSABLE:
        case STARK_STORAGE_REMOVED:
            schedule(c, now_ms, c->timing.retry_ms);
            step.action = STORAGE_ACT_TRY_MOUNT;
            break;
        case STARK_STORAGE_MOUNTED:
            schedule(c, now_ms, c->timing.probe_ms);
            step.action = STORAGE_ACT_PROBE;
            break;
        case STARK_STORAGE_EJECTED:
            /* Never mounts by itself: it only notices the card being pulled,
             * so the next insertion mounts again (ADR-0018 §5). */
            schedule(c, now_ms, c->timing.retry_ms);
            step.action = STORAGE_ACT_PRESENT_CHECK;
            break;
        case STARK_STORAGE_NOT_FITTED:
            break;
    }
    return step;
}

static storage_step_t on_mount_result(storage_core_t *c, stark_err_t result, uint32_t now_ms)
{
    storage_step_t step = {STORAGE_ACT_NONE, false};
    if (result == STARK_OK) {
        step.changed = enter(c, STARK_STORAGE_MOUNTED);
        schedule(c, now_ms, c->timing.probe_ms);
    } else if (result == STARK_ERR_NOT_FOUND) {
        /* A lost card stays REMOVED (still an error to show) until a mount
         * succeeds; an unusable card that stops answering was pulled. */
        if (c->state != STARK_STORAGE_REMOVED) {
            step.changed = enter(c, STARK_STORAGE_ABSENT);
        }
    } else {
        step.changed = enter(c, STARK_STORAGE_UNUSABLE);
    }
    return step;
}

static storage_step_t on_probe_result(storage_core_t *c, stark_err_t result, uint32_t now_ms)
{
    storage_step_t step = {STORAGE_ACT_NONE, false};
    if (result == STARK_OK) {
        return step;
    }
    c->generation++;
    step.changed = enter(c, STARK_STORAGE_REMOVED);
    step.action = STORAGE_ACT_UNMOUNT;
    schedule(c, now_ms, c->timing.retry_ms);
    return step;
}

static storage_step_t on_present_result(storage_core_t *c, stark_err_t result)
{
    storage_step_t step = {STORAGE_ACT_NONE, false};
    if (c->state == STARK_STORAGE_EJECTED && result != STARK_OK) {
        step.changed = enter(c, STARK_STORAGE_ABSENT); /* the user pulled it */
    }
    return step;
}

static storage_step_t on_req_mount(storage_core_t *c, uint32_t now_ms)
{
    storage_step_t step = {STORAGE_ACT_NONE, false};
    if (c->state != STARK_STORAGE_MOUNTED) {
        schedule(c, now_ms, c->timing.retry_ms);
        step.action = STORAGE_ACT_TRY_MOUNT;
    }
    return step;
}

static storage_step_t on_req_eject(storage_core_t *c, uint32_t now_ms)
{
    storage_step_t step = {STORAGE_ACT_NONE, false};
    if (c->state == STARK_STORAGE_MOUNTED) {
        c->generation++;
        step.changed = enter(c, STARK_STORAGE_EJECTED);
        step.action = STORAGE_ACT_UNMOUNT;
        schedule(c, now_ms, c->timing.retry_ms);
    }
    return step;
}

storage_step_t storage_core_step(storage_core_t *c, storage_input_t in, stark_err_t result,
                                 uint32_t now_ms)
{
    storage_step_t none = {STORAGE_ACT_NONE, false};
    if (c->state == STARK_STORAGE_NOT_FITTED && in != STORAGE_IN_TICK) {
        return none; /* a tick in NOT_FITTED is a no-op of its own (on_tick) */
    }
    switch (in) {
        case STORAGE_IN_TICK:
            return on_tick(c, now_ms);
        case STORAGE_IN_MOUNT_RESULT:
            return on_mount_result(c, result, now_ms);
        case STORAGE_IN_PROBE_RESULT:
            return on_probe_result(c, result, now_ms);
        case STORAGE_IN_PRESENT_RESULT:
            return on_present_result(c, result);
        case STORAGE_IN_REQ_MOUNT:
            return on_req_mount(c, now_ms);
        case STORAGE_IN_REQ_EJECT:
            return on_req_eject(c, now_ms);
        case STORAGE_IN_IO_ERROR:
            if (c->state == STARK_STORAGE_MOUNTED) {
                c->due_now = true; /* confirm with a probe at once */
            }
            return none;
    }
    return none;
}

uint32_t storage_core_wait_ms(const storage_core_t *c, uint32_t now_ms)
{
    if (c->state == STARK_STORAGE_NOT_FITTED) {
        return UINT32_MAX;
    }
    if (is_due(c, now_ms)) {
        return 0;
    }
    return c->due_ms - now_ms;
}

const char *stark_storage_state_str(stark_storage_state_t state)
{
    switch (state) {
        case STARK_STORAGE_NOT_FITTED:
            return "not fitted";
        case STARK_STORAGE_ABSENT:
            return "no card";
        case STARK_STORAGE_MOUNTED:
            return "mounted";
        case STARK_STORAGE_UNUSABLE:
            return "unusable";
        case STARK_STORAGE_REMOVED:
            return "removed";
        case STARK_STORAGE_EJECTED:
            return "ejected";
    }
    return "?";
}
