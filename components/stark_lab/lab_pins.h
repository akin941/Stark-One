/*
 * lab_pins.h — the lab service's pad decisions (TASKS.md STARK-0701, ADR-0023 §3): the offer,
 * the hold, the arm, the contention check and the hot-plug withdrawal. Pure, over an injected
 * I/O table, with no lock and no allocation; stark_lab.c binds it to stark_module,
 * stark_lab_transport, the log and the event bus, and the host tests bind it to the fake pads
 * (test/fake/fake_lab.c). The pin numbers come from the board's offer table (stark_board.h),
 * never from here.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_board.h"
#include "lab_pwm.h"
#include "stark_lab.h"
#include "stark_module.h"

#define LAB_OWNER_MAX       16u
#define LAB_CONTENTION_US   1000u /* the pad is read back this long after it was driven */
#define LAB_PROBE_SETTLE_US 2000u /* each pull of the state probe is applied this long */

typedef enum {
    LAB_LOG_WITHDRAWN, /* a = pin, b = slot: no longer offered */
    LAB_LOG_CONTENTION /* a = pin: the pad did not follow what it was driven to */
} lab_log_t;

typedef struct {
    void *ctx;
    size_t (*list)(void *ctx, stark_module_entry_t *out, size_t max);
    stark_err_t (*pad_in)(void *ctx, int pin, uint8_t pull);
    stark_err_t (*pad_out)(void *ctx, int pin, bool level);
    stark_err_t (*pad_read)(void *ctx, int pin, bool *level);
    stark_err_t (*pad_hiz)(void *ctx, int pin);
    stark_err_t (*pwm_start)(void *ctx, int pin, uint32_t hz, uint8_t duty_pct);
    stark_err_t (*pwm_stop)(void *ctx, int pin); /* stops the channel and releases the pad */
    stark_err_t (*pulse)(void *ctx, int pin, bool level, uint32_t us);
    void (*delay_us)(void *ctx, uint32_t us);
    void (*log)(void *ctx, lab_log_t what, int a, int b);
} lab_io_t;

typedef struct {
    bool held;
    bool armed;
    char owner[LAB_OWNER_MAX];
    stark_lab_mode_t mode;
    stark_lab_pull_t pull;
    bool level;
    uint32_t hz;
    uint32_t duty_pct;
} lab_pin_hold_t;

typedef struct {
    lab_io_t io;
    stark_board_lab_pin_t tab[STARK_BOARD_LAB_PINS_MAX];
    lab_pin_hold_t hold[STARK_BOARD_LAB_PINS_MAX];
    size_t n;
    int pwm_idx;      /* the table index that has the PWM, or -1 (one pin at a time) */
    uint32_t offered; /* bit i: tab[i] was offered at the last look */
    bool changed;     /* the offer changed or a held pin was withdrawn, not yet taken */
} lab_pins_t;

/* Builds the offer table from the board's pin map and takes a first look at the offer. */
void lab_pins_init(lab_pins_t *p, const lab_io_t *io, const stark_board_pins_t *pins);

/* The offered pins as a bit mask over the table, now. */
uint32_t lab_pins_offered_mask(lab_pins_t *p);

/* The table with the flags as of now; returns how many entries exist. */
size_t lab_pins_list(lab_pins_t *p, stark_lab_pin_t *out, size_t max);

stark_err_t lab_pins_acquire(lab_pins_t *p, int pin, const char *owner);
void lab_pins_release(lab_pins_t *p, int pin);
void lab_pins_release_all(lab_pins_t *p);
stark_err_t lab_pins_arm(lab_pins_t *p, int pin);
stark_err_t lab_pins_input(lab_pins_t *p, int pin, stark_lab_pull_t pull);
stark_err_t lab_pins_hiz(lab_pins_t *p, int pin);
stark_err_t lab_pins_read(lab_pins_t *p, int pin, bool *level);
stark_err_t lab_pins_state(lab_pins_t *p, int pin, stark_lab_pinstate_t *st);
stark_err_t lab_pins_write(lab_pins_t *p, int pin, bool level);
stark_err_t lab_pins_toggle(lab_pins_t *p, int pin);
stark_err_t lab_pins_info(lab_pins_t *p, int pin, stark_lab_info_t *out);
stark_err_t lab_pins_pwm_start(lab_pins_t *p, int pin, uint32_t hz, uint32_t duty_pct);
void lab_pins_pwm_stop(lab_pins_t *p);
stark_err_t lab_pins_pulse(lab_pins_t *p, int pin, bool level, uint32_t us);

/*
 * Looks at the offer again: every held pin that is gone goes to hi-Z and is withdrawn, every
 * pin that stopped being offered is logged, and the offered mask follows. Returns true when
 * the offer or a hold changed (and clears that flag).
 */
bool lab_pins_refresh(lab_pins_t *p);

/* True once after the offer changed or a held pin was withdrawn (also by a pin call). */
bool lab_pins_take_changed(lab_pins_t *p);
