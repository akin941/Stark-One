/*
 * stark_input.h — key identities and key actions (ARCHITECTURE.md §6.6).
 *
 * Host-includable (no ESP-IDF types). The debounce/repeat state machine that
 * produces these actions is input_core.h (STARK-0011); the service that
 * samples the GPIOs and publishes STARK_EVT_KEY is stark_input_start()
 * below (input_service.c, STARK-0012, target-only).
 */
#pragma once

#include "stark_board.h"

/* Index order matches stark_board_pins_t.key[] (UP DOWN LEFT RIGHT OK BACK)
 * and bit order in the raw bitmap input_core_update() takes. */
typedef enum {
    STARK_KEY_UP,
    STARK_KEY_DOWN,
    STARK_KEY_LEFT,
    STARK_KEY_RIGHT,
    STARK_KEY_OK,
    STARK_KEY_BACK,
} stark_key_t;

/* NOTE: the number of keys is the board's STARK_KEY_COUNT (stark_board.h),
 * not a trailing enum member, so the two can never drift apart. */
_Static_assert(STARK_KEY_COUNT == STARK_KEY_BACK + 1, "stark_key_t must cover every board key");

typedef enum {
    STARK_KEY_PRESS,   /* debounced press */
    STARK_KEY_RELEASE, /* debounced release */
    STARK_KEY_REPEAT,  /* held: after 400 ms, then every 120 ms — UP/DOWN/LEFT/RIGHT only */
    STARK_KEY_LONG,    /* held 500 ms: emitted once per press */
    STARK_KEY_SHORT,   /* released before LONG was emitted */
} stark_key_action_t;

/*
 * Starts the input service: a CONFIG_STARK_INPUT_POLL_MS (5 ms) periodic
 * esp_timer, in the esp_timer task, that reads the six key GPIOs through
 * stark_hal (active-low, pull-ups from stark_board_init()), runs the core
 * and publishes one STARK_EVT_KEY per action, logging `key: <NAME>
 * <action>` at DEBUG. A full bus drops its oldest event (stats.dropped);
 * the callback never retries. Needs stark_board_init() and
 * stark_event_init() first. STARK_ERR_STATE if already started; an
 * esp_timer failure is translated with stark_err_from_esp().
 */
stark_err_t stark_input_start(void);
