/*
 * stark_input.h — key identities and key actions (ARCHITECTURE.md §6.6).
 *
 * Pure types, host-includable. The debounce/repeat state machine that
 * produces these actions is input_core.h (STARK-0011); the service that
 * samples the GPIOs and publishes STARK_EVT_KEY is STARK-0012's.
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
