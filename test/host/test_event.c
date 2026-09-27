/*
 * test_event.c — host unit tests for the pure event bus core (STARK-0009).
 *
 * Cases: TESTING.md §2 "Event bus" plus publish-from-handler (TASKS.md
 * STARK-0009). The injected lock is a no-op that also checks it is never
 * taken twice and never held while a handler runs — the property that
 * makes publishing from a handler safe under the port's non-recursive
 * mutex (STARK-0010).
 */
#include "stark_event_core.h"
#include "unity.h"

#define CAP     4
#define LOG_MAX 32

static stark_event_t s_storage[CAP];
static stark_event_core_t s_bus;

/* ---- Injected lock ---------------------------------------------------- */

static int s_lock_tag; /* its address is the lock ctx */
static int s_lock_depth;
static int s_lock_calls;

static void test_lock(void *ctx)
{
    TEST_ASSERT_EQUAL_PTR(&s_lock_tag, ctx);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_lock_depth, "lock taken while already held");
    s_lock_depth++;
    s_lock_calls++;
}

static void test_unlock(void *ctx)
{
    TEST_ASSERT_EQUAL_PTR(&s_lock_tag, ctx);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_lock_depth, "unlock without lock");
    s_lock_depth--;
}

static const stark_lock_t k_lock = {.lock = test_lock, .unlock = test_unlock, .ctx = &s_lock_tag};

/* ---- Recording handler ------------------------------------------------ */

typedef struct {
    stark_event_t events[LOG_MAX];
    size_t n;
} recorder_t;

static recorder_t s_rec_a;
static recorder_t s_rec_b;

static void record(const stark_event_t *e, void *ctx)
{
    recorder_t *r = ctx;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_lock_depth, "handler called with the lock held");
    TEST_ASSERT_LESS_THAN(LOG_MAX, r->n);
    r->events[r->n++] = *e;
}

/* ---- Event helpers ---------------------------------------------------- */

static stark_event_t key_event(uint8_t key)
{
    stark_event_t e = {.type = STARK_EVT_KEY, .ts_us = 1000u + key};
    e.key.key = key;
    e.key.action = 2;
    e.key.repeat = 1;
    return e;
}

static void publish_ok(stark_event_core_t *bus, stark_event_t e)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_publish(bus, &e));
}

static void assert_keys(const recorder_t *r, const uint8_t *keys, size_t n)
{
    TEST_ASSERT_EQUAL_size_t(n, r->n);
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL(STARK_EVT_KEY, r->events[i].type);
        TEST_ASSERT_EQUAL_UINT8(keys[i], r->events[i].key.key);
    }
}

void setUp(void)
{
    s_lock_depth = 0;
    s_lock_calls = 0;
    s_rec_a = (recorder_t){.n = 0};
    s_rec_b = (recorder_t){.n = 0};
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_init(&s_bus, s_storage, CAP, k_lock));
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_lock_depth, "lock left held");
}

/* ---- init --------------------------------------------------------------- */

void test_init_rejects_invalid_arguments(void)
{
    stark_event_core_t bus;
    stark_lock_t no_lock = {.lock = NULL, .unlock = test_unlock, .ctx = NULL};
    stark_lock_t no_unlock = {.lock = test_lock, .unlock = NULL, .ctx = NULL};

    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_event_core_init(NULL, s_storage, CAP, k_lock));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_event_core_init(&bus, NULL, CAP, k_lock));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_event_core_init(&bus, s_storage, 0, k_lock));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_event_core_init(&bus, s_storage, CAP, no_lock));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_event_core_init(&bus, s_storage, CAP, no_unlock));
}

void test_init_again_resets_queue_subscribers_and_stats(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(&s_bus, STARK_EVT_MASK(STARK_EVT_KEY),
                                                           record, &s_rec_a));
    for (uint8_t k = 0; k < CAP + 1; k++) {
        publish_ok(&s_bus, key_event(k));
    }

    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_init(&s_bus, s_storage, CAP, k_lock));

    TEST_ASSERT_EQUAL_UINT32(0, s_bus.stats.published);
    TEST_ASSERT_EQUAL_UINT32(0, s_bus.stats.dropped);
    TEST_ASSERT_EQUAL_UINT32(0, s_bus.stats.max_depth);
    publish_ok(&s_bus, key_event(9));
    TEST_ASSERT_EQUAL_size_t(1, stark_event_core_dispatch(&s_bus, UINT32_MAX));
    TEST_ASSERT_EQUAL_size_t(0, s_rec_a.n); /* old subscriber is gone */
}

/* ---- publish / dispatch ------------------------------------------------ */

void test_empty_dispatch_is_a_noop(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(&s_bus, UINT32_MAX, record, &s_rec_a));

    TEST_ASSERT_EQUAL_size_t(0, stark_event_core_dispatch(&s_bus, UINT32_MAX));

    TEST_ASSERT_EQUAL_size_t(0, s_rec_a.n);
    TEST_ASSERT_EQUAL_UINT32(0, s_bus.stats.published);
    TEST_ASSERT_EQUAL_UINT32(0, s_bus.stats.dropped);
    TEST_ASSERT_EQUAL_UINT32(0, s_bus.stats.max_depth);
}

void test_dispatch_delivers_in_publish_order(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(&s_bus, STARK_EVT_MASK(STARK_EVT_KEY),
                                                           record, &s_rec_a));
    publish_ok(&s_bus, key_event(3));
    publish_ok(&s_bus, key_event(1));
    publish_ok(&s_bus, key_event(2));

    TEST_ASSERT_EQUAL_size_t(3, stark_event_core_dispatch(&s_bus, UINT32_MAX));

    const uint8_t want[] = {3, 1, 2};
    assert_keys(&s_rec_a, want, 3);
    TEST_ASSERT_EQUAL_UINT32(3, s_bus.stats.published);
    TEST_ASSERT_EQUAL_UINT32(3, s_bus.stats.max_depth);
}

void test_every_payload_and_timestamp_survive_the_ring(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(&s_bus, UINT32_MAX, record, &s_rec_a));

    stark_event_t key = key_event(5);
    stark_event_t tick = {.type = STARK_EVT_TICK, .ts_us = UINT64_MAX};
    stark_event_t app = {.type = STARK_EVT_APP_REQUEST, .ts_us = 42};
    app.app.id = 0xDEADBEEFu;
    app.app.arg = -7;
    stark_event_t sys = {.type = STARK_EVT_SYSTEM, .ts_us = 0};
    sys.system.id = 3;
    sys.system.arg = INT32_MIN;
    publish_ok(&s_bus, key);
    publish_ok(&s_bus, tick);
    publish_ok(&s_bus, app);
    publish_ok(&s_bus, sys);

    TEST_ASSERT_EQUAL_size_t(4, stark_event_core_dispatch(&s_bus, UINT32_MAX));

    TEST_ASSERT_EQUAL_size_t(4, s_rec_a.n);
    TEST_ASSERT_EQUAL(STARK_EVT_KEY, s_rec_a.events[0].type);
    TEST_ASSERT_EQUAL_UINT64(1005, s_rec_a.events[0].ts_us);
    TEST_ASSERT_EQUAL_UINT8(5, s_rec_a.events[0].key.key);
    TEST_ASSERT_EQUAL_UINT8(2, s_rec_a.events[0].key.action);
    TEST_ASSERT_EQUAL_UINT8(1, s_rec_a.events[0].key.repeat);
    TEST_ASSERT_EQUAL(STARK_EVT_TICK, s_rec_a.events[1].type);
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, s_rec_a.events[1].ts_us);
    TEST_ASSERT_EQUAL(STARK_EVT_APP_REQUEST, s_rec_a.events[2].type);
    TEST_ASSERT_EQUAL_UINT64(42, s_rec_a.events[2].ts_us);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, s_rec_a.events[2].app.id);
    TEST_ASSERT_EQUAL_INT32(-7, s_rec_a.events[2].app.arg);
    TEST_ASSERT_EQUAL(STARK_EVT_SYSTEM, s_rec_a.events[3].type);
    TEST_ASSERT_EQUAL_UINT64(0, s_rec_a.events[3].ts_us);
    TEST_ASSERT_EQUAL_UINT32(3, s_rec_a.events[3].system.id);
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, s_rec_a.events[3].system.arg);
}

void test_publish_rejects_invalid_events_without_queueing(void)
{
    stark_event_t none = {.type = STARK_EVT_NONE};
    stark_event_t past_last = {.type = (stark_evt_type_t)(STARK_EVT_TYPE_LAST + 1)};
    stark_event_t key = key_event(1);

    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_event_core_publish(NULL, &key));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_event_core_publish(&s_bus, NULL));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_event_core_publish(&s_bus, &none));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_event_core_publish(&s_bus, &past_last));

    TEST_ASSERT_EQUAL_UINT32(0, s_bus.stats.published);
    TEST_ASSERT_EQUAL_size_t(0, stark_event_core_dispatch(&s_bus, UINT32_MAX));
}

void test_dispatch_honours_max_events(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(&s_bus, STARK_EVT_MASK(STARK_EVT_KEY),
                                                           record, &s_rec_a));
    publish_ok(&s_bus, key_event(1));
    publish_ok(&s_bus, key_event(2));
    publish_ok(&s_bus, key_event(3));

    TEST_ASSERT_EQUAL_size_t(0, stark_event_core_dispatch(&s_bus, 0));
    TEST_ASSERT_EQUAL_size_t(1, stark_event_core_dispatch(&s_bus, 1));
    const uint8_t first[] = {1};
    assert_keys(&s_rec_a, first, 1);

    TEST_ASSERT_EQUAL_size_t(2, stark_event_core_dispatch(&s_bus, 10));
    const uint8_t all[] = {1, 2, 3};
    assert_keys(&s_rec_a, all, 3);
}

void test_dispatch_with_null_bus_returns_zero(void)
{
    TEST_ASSERT_EQUAL_size_t(0, stark_event_core_dispatch(NULL, UINT32_MAX));
}

void test_event_with_no_subscriber_is_still_consumed(void)
{
    publish_ok(&s_bus, key_event(1));
    publish_ok(&s_bus, key_event(2));

    TEST_ASSERT_EQUAL_size_t(2, stark_event_core_dispatch(&s_bus, UINT32_MAX));
    TEST_ASSERT_EQUAL_size_t(0, stark_event_core_dispatch(&s_bus, UINT32_MAX));
}

/* ---- capacity, overflow, wrap-around ---------------------------------- */

void test_overflow_drops_the_oldest_and_counts_it(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(&s_bus, STARK_EVT_MASK(STARK_EVT_KEY),
                                                           record, &s_rec_a));
    for (uint8_t k = 0; k < CAP + 2; k++) { /* two more than fit */
        publish_ok(&s_bus, key_event(k));
    }

    TEST_ASSERT_EQUAL_UINT32(CAP + 2, s_bus.stats.published);
    TEST_ASSERT_EQUAL_UINT32(2, s_bus.stats.dropped);
    TEST_ASSERT_EQUAL_UINT32(CAP, s_bus.stats.max_depth);

    TEST_ASSERT_EQUAL_size_t(CAP, stark_event_core_dispatch(&s_bus, UINT32_MAX));
    const uint8_t newest[] = {2, 3, 4, 5}; /* 0 and 1 were dropped */
    assert_keys(&s_rec_a, newest, CAP);
}

void test_exactly_full_ring_drops_nothing(void)
{
    for (uint8_t k = 0; k < CAP; k++) {
        publish_ok(&s_bus, key_event(k));
    }
    TEST_ASSERT_EQUAL_UINT32(0, s_bus.stats.dropped);
    TEST_ASSERT_EQUAL_size_t(CAP, stark_event_core_dispatch(&s_bus, UINT32_MAX));
}

void test_capacity_one_ring_keeps_only_the_newest(void)
{
    stark_event_t storage[1];
    stark_event_core_t bus;
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_init(&bus, storage, 1, k_lock));
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(&bus, STARK_EVT_MASK(STARK_EVT_KEY),
                                                           record, &s_rec_a));
    publish_ok(&bus, key_event(1));
    publish_ok(&bus, key_event(2));
    publish_ok(&bus, key_event(3));

    TEST_ASSERT_EQUAL_UINT32(2, bus.stats.dropped);
    TEST_ASSERT_EQUAL_UINT32(1, bus.stats.max_depth);
    TEST_ASSERT_EQUAL_size_t(1, stark_event_core_dispatch(&bus, UINT32_MAX));
    const uint8_t newest[] = {3};
    assert_keys(&s_rec_a, newest, 1);
}

void test_order_holds_across_repeated_wrap_around(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(&s_bus, STARK_EVT_MASK(STARK_EVT_KEY),
                                                           record, &s_rec_a));
    uint8_t next = 0;
    for (int round = 0; round < 7; round++) { /* 3 per round on a ring of 4 */
        s_rec_a.n = 0;
        uint8_t first = next;
        for (int i = 0; i < 3; i++) {
            publish_ok(&s_bus, key_event(next++));
        }
        TEST_ASSERT_EQUAL_size_t(3, stark_event_core_dispatch(&s_bus, UINT32_MAX));
        const uint8_t want[] = {first, (uint8_t)(first + 1), (uint8_t)(first + 2)};
        assert_keys(&s_rec_a, want, 3);
    }
    TEST_ASSERT_EQUAL_UINT32(0, s_bus.stats.dropped);
    TEST_ASSERT_EQUAL_UINT32(3, s_bus.stats.max_depth);
}

/* ---- subscribers ------------------------------------------------------ */

static int s_order_log[LOG_MAX];
static size_t s_order_n;

static void log_id(const stark_event_t *e, void *ctx)
{
    (void)e;
    TEST_ASSERT_LESS_THAN(LOG_MAX, s_order_n);
    s_order_log[s_order_n++] = *(const int *)ctx;
}

void test_type_mask_filters_what_each_subscriber_sees(void)
{
    recorder_t tick_rec = {.n = 0};
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(&s_bus, STARK_EVT_MASK(STARK_EVT_KEY),
                                                           record, &s_rec_a));
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_event_core_subscribe(&s_bus,
                                                 STARK_EVT_MASK(STARK_EVT_SYSTEM) |
                                                     STARK_EVT_MASK(STARK_EVT_APP_REQUEST),
                                                 record, &s_rec_b));
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(&s_bus, STARK_EVT_MASK(STARK_EVT_TICK),
                                                           record, &tick_rec));

    publish_ok(&s_bus, key_event(1));
    publish_ok(&s_bus, (stark_event_t){.type = STARK_EVT_TICK});
    publish_ok(&s_bus, (stark_event_t){.type = STARK_EVT_SYSTEM});
    publish_ok(&s_bus, (stark_event_t){.type = STARK_EVT_APP_REQUEST});
    TEST_ASSERT_EQUAL_size_t(4, stark_event_core_dispatch(&s_bus, UINT32_MAX));

    TEST_ASSERT_EQUAL_size_t(1, s_rec_a.n);
    TEST_ASSERT_EQUAL(STARK_EVT_KEY, s_rec_a.events[0].type);
    TEST_ASSERT_EQUAL_size_t(2, s_rec_b.n);
    TEST_ASSERT_EQUAL(STARK_EVT_SYSTEM, s_rec_b.events[0].type);
    TEST_ASSERT_EQUAL(STARK_EVT_APP_REQUEST, s_rec_b.events[1].type);
    TEST_ASSERT_EQUAL_size_t(1, tick_rec.n);
    TEST_ASSERT_EQUAL(STARK_EVT_TICK, tick_rec.events[0].type);
}

void test_matching_subscribers_run_in_subscription_order(void)
{
    static const int ids[] = {1, 2, 3};
    s_order_n = 0;
    for (size_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(STARK_OK,
                          stark_event_core_subscribe(&s_bus, STARK_EVT_MASK(STARK_EVT_KEY), log_id,
                                                     (void *)&ids[i]));
    }
    publish_ok(&s_bus, key_event(1));
    publish_ok(&s_bus, key_event(2));
    TEST_ASSERT_EQUAL_size_t(2, stark_event_core_dispatch(&s_bus, UINT32_MAX));

    const int want[] = {1, 2, 3, 1, 2, 3};
    TEST_ASSERT_EQUAL_size_t(6, s_order_n);
    TEST_ASSERT_EQUAL_INT_ARRAY(want, s_order_log, 6);
}

void test_subscribe_rejects_invalid_arguments_and_a_ninth_subscriber(void)
{
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_event_core_subscribe(NULL, UINT32_MAX, record, &s_rec_a));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_event_core_subscribe(&s_bus, UINT32_MAX, NULL, &s_rec_a));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_event_core_subscribe(&s_bus, 0, record, &s_rec_a));

    for (int i = 0; i < STARK_EVENT_CORE_MAX_SUBS; i++) {
        TEST_ASSERT_EQUAL(STARK_OK,
                          stark_event_core_subscribe(&s_bus, UINT32_MAX, record, &s_rec_a));
    }
    TEST_ASSERT_EQUAL(STARK_ERR_NO_MEM,
                      stark_event_core_subscribe(&s_bus, UINT32_MAX, record, &s_rec_b));

    publish_ok(&s_bus, key_event(1));
    TEST_ASSERT_EQUAL_size_t(1, stark_event_core_dispatch(&s_bus, UINT32_MAX));
    TEST_ASSERT_EQUAL_size_t(STARK_EVENT_CORE_MAX_SUBS, s_rec_a.n); /* all 8, the 9th never */
    TEST_ASSERT_EQUAL_size_t(0, s_rec_b.n);
}

/* ---- re-entrancy: handlers calling back into the bus ------------------- */

static void republish_tick_once(const stark_event_t *e, void *ctx)
{
    record(e, &s_rec_a);
    if (e->type == STARK_EVT_KEY) {
        stark_event_t tick = {.type = STARK_EVT_TICK, .ts_us = 77};
        TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_publish(ctx, &tick));
    }
}

void test_publish_from_handler_is_delivered_on_the_next_dispatch(void)
{
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_event_core_subscribe(&s_bus, UINT32_MAX, republish_tick_once, &s_bus));
    publish_ok(&s_bus, key_event(1));

    TEST_ASSERT_EQUAL_size_t(1, stark_event_core_dispatch(&s_bus, UINT32_MAX));
    TEST_ASSERT_EQUAL_size_t(1, s_rec_a.n); /* only the key, not the tick */
    TEST_ASSERT_EQUAL(STARK_EVT_KEY, s_rec_a.events[0].type);

    TEST_ASSERT_EQUAL_size_t(1, stark_event_core_dispatch(&s_bus, UINT32_MAX));
    TEST_ASSERT_EQUAL_size_t(2, s_rec_a.n);
    TEST_ASSERT_EQUAL(STARK_EVT_TICK, s_rec_a.events[1].type);
    TEST_ASSERT_EQUAL_UINT64(77, s_rec_a.events[1].ts_us);
}

static void publish_two_on_key_one(const stark_event_t *e, void *ctx)
{
    record(e, &s_rec_a);
    if (e->type == STARK_EVT_KEY && e->key.key == 1) {
        publish_ok(ctx, key_event(10));
        publish_ok(ctx, key_event(11));
        /* Key 10 was written into the slot key 1 came from: the handler's
         * event must be a copy, not a view of the ring. */
        TEST_ASSERT_EQUAL_UINT8(1, e->key.key);
        TEST_ASSERT_EQUAL_UINT64(1001, e->ts_us);
    }
}

void test_handler_publishes_that_overflow_the_ring_still_wait_for_next_dispatch(void)
{
    stark_event_t storage[2];
    stark_event_core_t bus;
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_init(&bus, storage, 2, k_lock));
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(&bus, STARK_EVT_MASK(STARK_EVT_KEY),
                                                           publish_two_on_key_one, &bus));
    publish_ok(&bus, key_event(1));
    publish_ok(&bus, key_event(2));

    /* Key 1's handler publishes 10 and 11 into a ring holding only key 2:
     * key 2 is dropped as the oldest, and 10/11 must not be delivered in
     * this same call even though they now fill the ring. */
    TEST_ASSERT_EQUAL_size_t(1, stark_event_core_dispatch(&bus, UINT32_MAX));
    const uint8_t first[] = {1};
    assert_keys(&s_rec_a, first, 1);
    TEST_ASSERT_EQUAL_UINT32(1, bus.stats.dropped);

    TEST_ASSERT_EQUAL_size_t(2, stark_event_core_dispatch(&bus, UINT32_MAX));
    const uint8_t all[] = {1, 10, 11};
    assert_keys(&s_rec_a, all, 3);
}

static void subscribe_b_once(const stark_event_t *e, void *ctx)
{
    record(e, &s_rec_a);
    if (s_rec_a.n == 1) {
        TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(ctx, STARK_EVT_MASK(STARK_EVT_KEY),
                                                               record, &s_rec_b));
    }
}

void test_subscribe_during_dispatch_takes_effect_from_the_next_event(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(&s_bus, STARK_EVT_MASK(STARK_EVT_KEY),
                                                           subscribe_b_once, &s_bus));
    publish_ok(&s_bus, key_event(1));
    publish_ok(&s_bus, key_event(2));

    TEST_ASSERT_EQUAL_size_t(2, stark_event_core_dispatch(&s_bus, UINT32_MAX));

    const uint8_t both[] = {1, 2};
    assert_keys(&s_rec_a, both, 2);
    const uint8_t second_only[] = {2}; /* subscribed while key 1 was being delivered */
    assert_keys(&s_rec_b, second_only, 1);
}

static size_t s_nested_result;

static void dispatch_from_handler(const stark_event_t *e, void *ctx)
{
    record(e, &s_rec_a);
    s_nested_result = stark_event_core_dispatch(ctx, UINT32_MAX);
}

void test_dispatch_from_a_handler_does_not_recurse(void)
{
    s_nested_result = 99;
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(&s_bus, STARK_EVT_MASK(STARK_EVT_KEY),
                                                           dispatch_from_handler, &s_bus));
    publish_ok(&s_bus, key_event(1));
    publish_ok(&s_bus, key_event(2));

    TEST_ASSERT_EQUAL_size_t(2, stark_event_core_dispatch(&s_bus, UINT32_MAX));

    TEST_ASSERT_EQUAL_size_t(0, s_nested_result);
    const uint8_t both[] = {1, 2}; /* each delivered once, by the outer call */
    assert_keys(&s_rec_a, both, 2);
}

void test_published_counter_wrap_keeps_the_dispatch_boundary(void)
{
    /* White-box: start the counter just below 2^32 so the handler's publish
     * wraps it in the middle of a dispatch. */
    s_bus.stats.published = UINT32_MAX - 2;
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_event_core_subscribe(&s_bus, UINT32_MAX, republish_tick_once, &s_bus));
    publish_ok(&s_bus, key_event(1));
    publish_ok(&s_bus, key_event(2)); /* published == UINT32_MAX */

    TEST_ASSERT_EQUAL_size_t(2, stark_event_core_dispatch(&s_bus, UINT32_MAX));
    TEST_ASSERT_EQUAL_UINT32(1, s_bus.stats.published); /* wrapped */
    const uint8_t keys[] = {1, 2};
    assert_keys(&s_rec_a, keys, 2); /* the two ticks were deferred */

    TEST_ASSERT_EQUAL_size_t(2, stark_event_core_dispatch(&s_bus, UINT32_MAX));
    TEST_ASSERT_EQUAL(STARK_EVT_TICK, s_rec_a.events[2].type);
    TEST_ASSERT_EQUAL(STARK_EVT_TICK, s_rec_a.events[3].type);
}

/* ---- reference model ----------------------------------------------------
 *
 * A trivially correct queue of publish ordinals (drop-oldest) runs beside
 * the real bus through thousands of random steps on rings of capacity 1-5.
 * Every event carries its ordinal in system.id and ts_us. Two handlers
 * publish at random while being dispatched, so publishes land mid-dispatch,
 * overflow the ring and wrap it; max_events varies from 0 to unlimited.
 * Every delivery must be exactly the model's oldest event and must predate
 * the dispatch() call delivering it.
 */

#define MODEL_MAX_CAP 5

typedef struct {
    stark_event_core_t *bus;
    uint32_t q[MODEL_MAX_CAP];
    size_t cap, head, count;
    uint32_t next; /* ordinal of the next publish == expected stats.published */
    uint32_t dropped;
    uint32_t max_depth;
    uint32_t start;     /* stats.published when the current dispatch() began */
    uint32_t delivered; /* events seen by model_first */
    uint32_t rng;
    bool handlers_publish;
} model_t;

static model_t s_m;

static uint32_t model_rand(uint32_t n)
{
    s_m.rng = s_m.rng * 1664525u + 1013904223u;
    return (s_m.rng >> 16) % n;
}

static void model_publish(void)
{
    stark_event_t e = {.type = STARK_EVT_SYSTEM, .ts_us = s_m.next};
    e.system.id = s_m.next;
    publish_ok(s_m.bus, e);

    if (s_m.count == s_m.cap) {
        s_m.head = (s_m.head + 1) % s_m.cap;
        s_m.count--;
        s_m.dropped++;
    }
    s_m.q[(s_m.head + s_m.count) % s_m.cap] = s_m.next++;
    s_m.count++;
    if (s_m.count > s_m.max_depth) {
        s_m.max_depth = (uint32_t)s_m.count;
    }
}

static void model_check_stats(void)
{
    TEST_ASSERT_EQUAL_UINT32(s_m.next, s_m.bus->stats.published);
    TEST_ASSERT_EQUAL_UINT32(s_m.dropped, s_m.bus->stats.dropped);
    TEST_ASSERT_EQUAL_UINT32(s_m.max_depth, s_m.bus->stats.max_depth);
}

/* Subscribed first: consumes the model's oldest event. */
static void model_first(const stark_event_t *e, void *ctx)
{
    (void)ctx;
    TEST_ASSERT_EQUAL_INT(0, s_lock_depth);
    TEST_ASSERT_TRUE_MESSAGE(s_m.count > 0, "delivered an event the model does not hold");
    uint32_t want = s_m.q[s_m.head];
    TEST_ASSERT_EQUAL_UINT32(want, e->system.id);
    TEST_ASSERT_EQUAL_UINT64(want, e->ts_us);
    TEST_ASSERT_TRUE_MESSAGE(want < s_m.start, "delivered an event published after dispatch began");
    s_m.head = (s_m.head + 1) % s_m.cap;
    s_m.count--;
    s_m.delivered++;
    for (uint32_t n = s_m.handlers_publish ? model_rand(3) : 0; n > 0; n--) {
        model_publish();
    }
}

/* Subscribed second: a second handler publishing during the same dispatch. */
static void model_second(const stark_event_t *e, void *ctx)
{
    (void)ctx;
    TEST_ASSERT_TRUE(e->system.id < s_m.start);
    for (uint32_t n = s_m.handlers_publish ? model_rand(2) : 0; n > 0; n--) {
        model_publish();
    }
}

static void model_dispatch(uint32_t max_events)
{
    s_m.start = s_m.next;
    uint32_t before = s_m.delivered;

    size_t n = stark_event_core_dispatch(s_m.bus, max_events);

    TEST_ASSERT_EQUAL_size_t(s_m.delivered - before, n);
    TEST_ASSERT_TRUE(n <= max_events);
    if (n < max_events) {
        /* Stopped short of max_events: nothing that predates the call is left. */
        TEST_ASSERT_TRUE(s_m.count == 0 || s_m.q[s_m.head] >= s_m.start);
    }
    model_check_stats();
}

void test_randomized_run_matches_a_reference_model(void)
{
    static const uint32_t max_choices[] = {0, 1, 2, 3, UINT32_MAX};

    for (size_t cap = 1; cap <= MODEL_MAX_CAP; cap++) {
        stark_event_t storage[MODEL_MAX_CAP];
        stark_event_core_t bus;
        s_m = (model_t){.bus = &bus, .cap = cap, .rng = 7919u * (uint32_t)cap};
        s_m.handlers_publish = true;
        TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_init(&bus, storage, cap, k_lock));
        TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(
                                        &bus, STARK_EVT_MASK(STARK_EVT_SYSTEM), model_first, NULL));
        TEST_ASSERT_EQUAL(
            STARK_OK,
            stark_event_core_subscribe(&bus, STARK_EVT_MASK(STARK_EVT_SYSTEM), model_second, NULL));

        for (int step = 0; step < 3000; step++) {
            if (model_rand(2) == 0) {
                for (uint32_t n = model_rand(4); n > 0; n--) {
                    model_publish();
                }
                model_check_stats();
            } else {
                model_dispatch(max_choices[model_rand(5)]);
            }
        }

        s_m.handlers_publish = false;
        model_dispatch(UINT32_MAX); /* drain */
        TEST_ASSERT_EQUAL_size_t(0, s_m.count);
        TEST_ASSERT_EQUAL_size_t(0, stark_event_core_dispatch(&bus, UINT32_MAX));

        /* Every event was delivered exactly once or counted as dropped. */
        TEST_ASSERT_EQUAL_UINT32(s_m.next, s_m.delivered + s_m.dropped);
        /* The run really did overflow and wrap the ring many times. */
        TEST_ASSERT_GREATER_THAN_UINT32(0, s_m.dropped);
        TEST_ASSERT_GREATER_THAN_UINT32(100u * (uint32_t)cap, s_m.next);
    }
}

/* ---- locking ----------------------------------------------------------- */

void test_every_operation_takes_and_releases_the_injected_lock(void)
{
    int before = s_lock_calls;
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_core_subscribe(&s_bus, UINT32_MAX, record, &s_rec_a));
    TEST_ASSERT_GREATER_THAN_INT(before, s_lock_calls);

    before = s_lock_calls;
    publish_ok(&s_bus, key_event(1));
    TEST_ASSERT_GREATER_THAN_INT(before, s_lock_calls);

    before = s_lock_calls;
    TEST_ASSERT_EQUAL_size_t(1, stark_event_core_dispatch(&s_bus, UINT32_MAX));
    TEST_ASSERT_GREATER_THAN_INT(before, s_lock_calls);
    TEST_ASSERT_EQUAL_INT(0, s_lock_depth);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_rejects_invalid_arguments);
    RUN_TEST(test_init_again_resets_queue_subscribers_and_stats);
    RUN_TEST(test_empty_dispatch_is_a_noop);
    RUN_TEST(test_dispatch_delivers_in_publish_order);
    RUN_TEST(test_every_payload_and_timestamp_survive_the_ring);
    RUN_TEST(test_publish_rejects_invalid_events_without_queueing);
    RUN_TEST(test_dispatch_honours_max_events);
    RUN_TEST(test_dispatch_with_null_bus_returns_zero);
    RUN_TEST(test_event_with_no_subscriber_is_still_consumed);
    RUN_TEST(test_overflow_drops_the_oldest_and_counts_it);
    RUN_TEST(test_exactly_full_ring_drops_nothing);
    RUN_TEST(test_capacity_one_ring_keeps_only_the_newest);
    RUN_TEST(test_order_holds_across_repeated_wrap_around);
    RUN_TEST(test_type_mask_filters_what_each_subscriber_sees);
    RUN_TEST(test_matching_subscribers_run_in_subscription_order);
    RUN_TEST(test_subscribe_rejects_invalid_arguments_and_a_ninth_subscriber);
    RUN_TEST(test_publish_from_handler_is_delivered_on_the_next_dispatch);
    RUN_TEST(test_handler_publishes_that_overflow_the_ring_still_wait_for_next_dispatch);
    RUN_TEST(test_subscribe_during_dispatch_takes_effect_from_the_next_event);
    RUN_TEST(test_dispatch_from_a_handler_does_not_recurse);
    RUN_TEST(test_published_counter_wrap_keeps_the_dispatch_boundary);
    RUN_TEST(test_randomized_run_matches_a_reference_model);
    RUN_TEST(test_every_operation_takes_and_releases_the_injected_lock);
    return UNITY_END();
}
