/*
 * stark_event_core.c — pure event bus core (TASKS.md STARK-0009).
 *
 * Includes nothing but its own headers (STARK-0009 AC #2): no ESP-IDF, no
 * FreeRTOS, no allocation. All shared state is touched only between
 * bus->lock.lock() and bus->lock.unlock(); handlers always run unlocked.
 */
#include "stark_event_core.h"

static bool type_valid(stark_evt_type_t type)
{
    uint32_t t = (uint32_t)type;
    return t > (uint32_t)STARK_EVT_NONE && t <= (uint32_t)STARK_EVT_TYPE_LAST;
}

static void bus_lock(stark_event_core_t *bus)
{
    bus->lock.lock(bus->lock.ctx);
}

static void bus_unlock(stark_event_core_t *bus)
{
    bus->lock.unlock(bus->lock.ctx);
}

stark_err_t stark_event_core_init(stark_event_core_t *bus, stark_event_t *storage, size_t capacity,
                                  stark_lock_t lock)
{
    if (bus == NULL || storage == NULL || capacity == 0 || lock.lock == NULL ||
        lock.unlock == NULL) {
        return STARK_ERR_INVALID_ARG;
    }

    *bus = (stark_event_core_t){
        .storage = storage,
        .capacity = capacity,
        .lock = lock,
    };
    return STARK_OK;
}

stark_err_t stark_event_core_publish(stark_event_core_t *bus, const stark_event_t *e)
{
    if (bus == NULL || e == NULL || !type_valid(e->type)) {
        return STARK_ERR_INVALID_ARG;
    }

    bus_lock(bus);
    if (bus->count == bus->capacity) {
        /* Full: drop the oldest (ARCHITECTURE.md §6.3), never the new one. */
        bus->head = (bus->head + 1) % bus->capacity;
        bus->count--;
        bus->stats.dropped++;
    }
    bus->storage[(bus->head + bus->count) % bus->capacity] = *e;
    bus->count++;
    bus->stats.published++;
    if (bus->count > bus->stats.max_depth) {
        bus->stats.max_depth = (uint32_t)bus->count;
    }
    bus_unlock(bus);
    return STARK_OK;
}

size_t stark_event_core_dispatch(stark_event_core_t *bus, uint32_t max_events)
{
    if (bus == NULL || max_events == 0) {
        return 0;
    }

    bus_lock(bus);
    if (bus->dispatching) {
        bus_unlock(bus);
        return 0;
    }
    bus->dispatching = true;
    /*
     * NOTE: the ring always holds the most recent `count` publishes, so the
     * newest (published - start) of them arrived after this call began and
     * must wait for the next dispatch(). Counting it this way stays correct
     * when a handler's publishes overflow the ring and push out events that
     * were queued before the call — a plain "count at start" would then
     * deliver the handler's own events in this call.
     */
    const uint32_t start = bus->stats.published;
    bus_unlock(bus);

    size_t delivered = 0;
    bool more = true;
    while (more && delivered < max_events) {
        stark_event_t e = {.type = STARK_EVT_NONE};
        stark_event_sub_t subs[STARK_EVENT_CORE_MAX_SUBS];
        size_t sub_count = 0;

        bus_lock(bus);
        size_t newer = (size_t)(uint32_t)(bus->stats.published - start);
        more = bus->count > newer;
        if (more) {
            e = bus->storage[bus->head];
            bus->head = (bus->head + 1) % bus->capacity;
            bus->count--;
            sub_count = bus->sub_count;
            for (size_t i = 0; i < sub_count; i++) {
                subs[i] = bus->subs[i];
            }
        }
        bus_unlock(bus);

        if (more) {
            uint32_t bit = STARK_EVT_MASK(e.type);
            for (size_t i = 0; i < sub_count; i++) {
                if ((subs[i].mask & bit) != 0) {
                    subs[i].fn(&e, subs[i].ctx);
                }
            }
            delivered++;
        }
    }

    bus_lock(bus);
    bus->dispatching = false;
    bus_unlock(bus);
    return delivered;
}

stark_err_t stark_event_core_subscribe(stark_event_core_t *bus, uint32_t type_mask,
                                       stark_event_handler_t fn, void *ctx)
{
    if (bus == NULL || fn == NULL || type_mask == 0) {
        return STARK_ERR_INVALID_ARG;
    }

    stark_err_t err = STARK_ERR_NO_MEM;
    bus_lock(bus);
    if (bus->sub_count < STARK_EVENT_CORE_MAX_SUBS) {
        bus->subs[bus->sub_count] = (stark_event_sub_t){
            .mask = type_mask,
            .fn = fn,
            .ctx = ctx,
        };
        bus->sub_count++;
        err = STARK_OK;
    }
    bus_unlock(bus);
    return err;
}
