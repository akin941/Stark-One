/*
 * ui_port_event.c — host UI test port: the stark_event.h API over the
 * production core (stark_event_core.c), single-threaded, so the lock is a
 * no-op and wait() never blocks: STARK_OK iff an event is pending.
 */
#include "stark_event.h"
#include "sdkconfig.h"
#include "stark_event_core.h"

static stark_event_core_t s_bus;
static stark_event_t s_storage[CONFIG_STARK_EVENT_QUEUE_LEN];
static bool s_ready;

static void no_lock(void *ctx)
{
    (void)ctx;
}

stark_err_t stark_event_init(void)
{
    if (s_ready) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = stark_event_core_init(&s_bus, s_storage, CONFIG_STARK_EVENT_QUEUE_LEN,
                                            (stark_lock_t){no_lock, no_lock, NULL});
    s_ready = err == STARK_OK;
    return err;
}

stark_err_t stark_event_publish(const stark_event_t *e)
{
    return s_ready ? stark_event_core_publish(&s_bus, e) : STARK_ERR_STATE;
}

stark_err_t stark_event_subscribe(uint32_t type_mask, stark_event_handler_t fn, void *ctx)
{
    return s_ready ? stark_event_core_subscribe(&s_bus, type_mask, fn, ctx) : STARK_ERR_STATE;
}

size_t stark_event_dispatch(uint32_t max_events)
{
    return s_ready ? stark_event_core_dispatch(&s_bus, max_events) : 0;
}

stark_err_t stark_event_wait(uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    return s_bus.count > 0 ? STARK_OK : STARK_ERR_TIMEOUT;
}

stark_err_t stark_event_stats(stark_event_stats_t *out)
{
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    *out = s_bus.stats;
    return STARK_OK;
}
