/*
 * stark_led.c — status LED heartbeat (TASKS.md STARK-0008).
 *
 * Board plumbing, so it lives in stark_board rather than in a service of
 * its own. The pin comes from stark_board_pins()->led_status — this file
 * names no GPIO number.
 *
 * NOTE: stark_board is L0 and stark_hal is L1 (ARCHITECTURE.md §2), so this
 * file cannot call stark_hal_gpio_write() without an upward dependency. It
 * drives the pin through the same ESP-IDF GPIO driver board_devkitc1.c
 * already uses, and relies on stark_board_init() having configured the pin
 * as an output — it does not configure it a second time.
 *
 * Pattern: 1 Hz, 10 % duty — one periodic esp_timer ticking every 100 ms;
 * the LED is on for tick 0 of every 10 and off for the other nine. A short
 * blink rather than a 50 % square wave makes a stuck-on or stuck-off LED
 * obvious.
 */
#include "stark_board.h"
#include <stddef.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"

#define STARK_LED_TICK_US  (100 * 1000) /* 100 ms */
#define STARK_LED_TICKS    10           /* 10 ticks = 1 s period (1 Hz) */
#define STARK_LED_ON_TICKS 1            /* 1 of 10 = 10 % duty */

static esp_timer_handle_t s_timer;
static unsigned s_tick;

static void heartbeat_cb(void *arg)
{
    (void)arg;
    s_tick = (s_tick + 1) % STARK_LED_TICKS;
    (void)gpio_set_level(stark_board_pins()->led_status, s_tick < STARK_LED_ON_TICKS ? 1 : 0);
}

stark_err_t stark_board_heartbeat_start(void)
{
    if (s_timer != NULL) {
        return STARK_ERR_STATE;
    }

    const esp_timer_create_args_t args = {
        .callback = heartbeat_cb,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "stark_led",
    };
    if (esp_timer_create(&args, &s_timer) != ESP_OK) {
        s_timer = NULL;
        return STARK_ERR_IO;
    }

    /* First blink immediately, not one tick after start. */
    s_tick = 0;
    (void)gpio_set_level(stark_board_pins()->led_status, 1);

    if (esp_timer_start_periodic(s_timer, STARK_LED_TICK_US) != ESP_OK) {
        (void)esp_timer_delete(s_timer);
        s_timer = NULL;
        (void)gpio_set_level(stark_board_pins()->led_status, 0);
        return STARK_ERR_IO;
    }

    return STARK_OK;
}
