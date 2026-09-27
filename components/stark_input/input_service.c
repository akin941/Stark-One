/*
 * input_service.c — samples the keys and publishes STARK_EVT_KEY
 * (TASKS.md STARK-0012, ARCHITECTURE.md §6.6). Target-only.
 *
 * All key semantics live in the pure core (input_core.c); this file only
 * turns GPIO levels into a bitmap on a timer and core actions into events.
 */
#include "stark_input.h"
#include "esp_timer.h"
#include "input_core.h"
#include "input_sample.h"
#include "sdkconfig.h"
#include "stark_event.h"
#include "stark_hal.h"
#include "stark_hal_esp.h"
#include "stark_log.h"

_Static_assert(CONFIG_STARK_INPUT_POLL_MS >= 1, "STARK_INPUT_POLL_MS must be at least 1");

static const char *const k_key_names[STARK_KEY_COUNT] = {"UP",    "DOWN", "LEFT",
                                                         "RIGHT", "OK",   "BACK"};

static input_core_t s_core;
static esp_timer_handle_t s_timer;

static const char *action_name(stark_key_action_t action)
{
    switch (action) {
        case STARK_KEY_PRESS:
            return "press";
        case STARK_KEY_RELEASE:
            return "release";
        case STARK_KEY_REPEAT:
            return "repeat";
        case STARK_KEY_LONG:
            return "long";
        case STARK_KEY_SHORT:
            return "short";
        case STARK_KEY_CHORD:
            return "chord";
    }
    return "?";
}

static void sample_cb(void *arg)
{
    (void)arg;
    uint64_t now_us = stark_hal_now_us();
    uint8_t raw = input_sample_raw(stark_board_pins());

    input_action_t out[INPUT_CORE_MAX_ACTIONS];
    size_t n =
        input_core_update(&s_core, raw, (uint32_t)(now_us / 1000u), out, INPUT_CORE_MAX_ACTIONS);
    for (size_t i = 0; i < n; i++) {
        stark_event_t e = {.type = STARK_EVT_KEY, .ts_us = now_us};
        e.key.key = (uint8_t)out[i].key;
        e.key.action = (uint8_t)out[i].action;
        e.key.repeat = out[i].repeat;
        /* NOTE: never retry — if the bus is full it drops its oldest event
         * and counts it in stats.dropped, which is the signal (TASKS.md). */
        (void)stark_event_publish(&e);
        if (out[i].action == STARK_KEY_CHORD) {
            STARK_LOGD("key", "OK+BACK chord");
        } else {
            STARK_LOGD("key", "%s %s", k_key_names[out[i].key], action_name(out[i].action));
        }
    }
}

stark_err_t stark_input_start(void)
{
    if (s_timer != NULL) {
        return STARK_ERR_STATE;
    }

    static const input_core_timing_t timing = {
        INPUT_DEBOUNCE_MS,
        INPUT_LONG_PRESS_MS,
        CONFIG_STARK_INPUT_REPEAT_DELAY_MS,
        CONFIG_STARK_INPUT_REPEAT_INTERVAL_MS,
    };
    stark_err_t err = input_core_init(&s_core, &timing); /* Kconfig ranges keep it valid */
    if (err != STARK_OK) {
        return err;
    }
    const esp_timer_create_args_t args = {
        .callback = sample_cb,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "stark_input",
    };
    err = stark_err_from_esp(esp_timer_create(&args, &s_timer));
    if (err != STARK_OK) {
        s_timer = NULL;
        return err;
    }
    err = stark_err_from_esp(
        esp_timer_start_periodic(s_timer, (uint64_t)CONFIG_STARK_INPUT_POLL_MS * 1000u));
    if (err != STARK_OK) {
        (void)esp_timer_delete(s_timer);
        s_timer = NULL;
    }
    return err;
}
