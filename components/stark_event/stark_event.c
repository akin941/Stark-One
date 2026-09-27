/*
 * stark_event.c — the global event bus: FreeRTOS port of the pure core
 * (TASKS.md STARK-0010, ARCHITECTURE.md §6.3). Target-only.
 *
 * Thin by construction: the ring in s_storage is the only event storage
 * (there is no FreeRTOS queue), the core does all ordering/overflow/
 * dispatch work, and this file only supplies what the core cannot — a
 * mutex as its injected lock, a wakeup for the UI task, and ts_us stamping.
 * The binary semaphore carries no events: it only wakes the consumer, which
 * then asks the ring whether anything is actually queued.
 * Every FreeRTOS object is static, so nothing here ever allocates.
 */
#include "stark_event.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "stark_event_core.h"
#include "stark_hal.h"

_Static_assert(CONFIG_STARK_EVENT_QUEUE_LEN >= 1, "STARK_EVENT_QUEUE_LEN must be at least 1");

static stark_event_t s_storage[CONFIG_STARK_EVENT_QUEUE_LEN];
static stark_event_core_t s_bus;
static StaticSemaphore_t s_mutex_buf;
static StaticSemaphore_t s_wake_buf;
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_wake;
/* Events handed out by stark_event_dispatch(), under s_mutex. */
static uint32_t s_consumed;
/* Written once by stark_event_init() before any user task exists. */
static bool s_ready;

static void bus_lock(void *ctx)
{
    /* Cannot fail: portMAX_DELAY blocks indefinitely (INCLUDE_vTaskSuspend). */
    (void)xSemaphoreTake((SemaphoreHandle_t)ctx, portMAX_DELAY);
}

static void bus_unlock(void *ctx)
{
    (void)xSemaphoreGive((SemaphoreHandle_t)ctx);
}

/*
 * NOTE: whether the ring holds an undispatched event, from public
 * quantities only: every accepted event (stats.published) is either
 * dropped (stats.dropped), handed out by dispatch() (s_consumed), or still
 * queued. Exact modulo 2^32, since at most CONFIG_STARK_EVENT_QUEUE_LEN are
 * queued. Called only by the consumer task, never while it is dispatching.
 */
static bool bus_pending(void)
{
    bus_lock(s_mutex);
    uint32_t queued = s_bus.stats.published - s_bus.stats.dropped - s_consumed;
    bus_unlock(s_mutex);
    return queued != 0;
}

/* pdMS_TO_TICKS() multiplies in 32-bit TickType_t and wraps past ~71 min;
 * convert in 64 bits instead, and stay below portMAX_DELAY, which FreeRTOS
 * treats as "forever" — this API has no infinite wait. */
static TickType_t ms_to_ticks(uint32_t ms)
{
    uint64_t ticks = (uint64_t)ms * configTICK_RATE_HZ / 1000u;
    return ticks >= portMAX_DELAY ? portMAX_DELAY - 1 : (TickType_t)ticks;
}

stark_err_t stark_event_init(void)
{
    if (s_ready) {
        return STARK_ERR_STATE;
    }

    /*
     * NOTE: a plain (non-recursive) mutex is enough: the core never takes the
     * lock twice and never holds it while a handler runs, so a handler's
     * publish() cannot deadlock. A binary rather than counting semaphore
     * (TASKS.md allows either): it coalesces a burst into one wakeup, which is
     * all a ring-backed bus needs — the ring, not a token count, says how much
     * work there is. The *Static constructors cannot fail when given a buffer
     * (xQueueGenericCreateStatic() returns NULL only for a NULL buffer or an
     * invalid length/item-size pair, which a semaphore never has), so nothing
     * below can leave s_ready half-set.
     */
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);
    s_wake = xSemaphoreCreateBinaryStatic(&s_wake_buf);

    const stark_lock_t lock = {.lock = bus_lock, .unlock = bus_unlock, .ctx = s_mutex};
    STARK_CHECK_RET(stark_event_core_init(&s_bus, s_storage, CONFIG_STARK_EVENT_QUEUE_LEN, lock));

    s_ready = true;
    return STARK_OK;
}

stark_err_t stark_event_publish(const stark_event_t *e)
{
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    if (e == NULL) {
        return STARK_ERR_INVALID_ARG;
    }

    stark_event_t stamped = *e;
    if (stamped.ts_us == 0) {
        stamped.ts_us = stark_hal_now_us();
    }
    STARK_CHECK_RET(stark_event_core_publish(&s_bus, &stamped));

    /* Fails harmlessly when already given: one wakeup covers a burst. */
    (void)xSemaphoreGive(s_wake);
    return STARK_OK;
}

stark_err_t stark_event_subscribe(uint32_t type_mask, stark_event_handler_t fn, void *ctx)
{
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    return stark_event_core_subscribe(&s_bus, type_mask, fn, ctx);
}

size_t stark_event_dispatch(uint32_t max_events)
{
    if (!s_ready) {
        return 0;
    }
    size_t n = stark_event_core_dispatch(&s_bus, max_events);
    bus_lock(s_mutex);
    s_consumed += (uint32_t)n;
    bus_unlock(s_mutex);
    return n;
}

stark_err_t stark_event_wait(uint32_t timeout_ms)
{
    if (!s_ready) {
        return STARK_ERR_STATE;
    }

    /*
     * NOTE: the ring decides, the semaphore only wakes. The ring is checked
     * before every take, so:
     *  - a publish landing between the check and the take has already given
     *    the semaphore, the take returns at once and the re-check sees the
     *    event — no lost wakeup;
     *  - a token left over from events that were dispatched without a wait()
     *    in between is consumed by the take, and the re-check finds the ring
     *    empty and blocks again — no stale STARK_OK.
     */
    TimeOut_t start;
    vTaskSetTimeOutState(&start);
    TickType_t remaining = ms_to_ticks(timeout_ms);
    for (;;) {
        if (bus_pending()) {
            return STARK_OK;
        }
        if (xSemaphoreTake(s_wake, remaining) != pdTRUE) {
            return STARK_ERR_TIMEOUT;
        }
        if (xTaskCheckForTimeOut(&start, &remaining) == pdTRUE) {
            return bus_pending() ? STARK_OK : STARK_ERR_TIMEOUT;
        }
    }
}

stark_err_t stark_event_stats(stark_event_stats_t *out)
{
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }

    bus_lock(s_mutex);
    *out = s_bus.stats;
    bus_unlock(s_mutex);
    return STARK_OK;
}
