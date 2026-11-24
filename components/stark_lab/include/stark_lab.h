/*
 * stark_lab.h — L3 lab service (TASKS.md STARK-0701…0707, ADR-0023): the one place that
 * decides which pins a lab tool may touch and checks every pin call against it. Today the
 * pad group (STARK-0701) and the PWM and pulse outputs (STARK-0702), whose user is the GPIO app;
 * later tasks add measurement, I2C, SPI and UART behind the same seam.
 *
 * Pins are named only through stark_board (stark_board_pin_class(), stark_board_lab_pins()):
 * a LAB pin is always offered, a SLOT pin only while its slot reads ABSENT, everything else
 * never (HARDWARE.md §2.4). Every call checks the offer first, then the hold, and a pin that
 * is not offered returns STARK_ERR_NOT_SUPPORTED before any hardware is touched. Driving
 * needs an arm after the app's confirmation; every pin is released to hi-Z (input, no pull)
 * on release, withdrawal and failure. Changes of the offer are published as
 * STARK_EVT_SYSTEM / STARK_SYS_LAB with system.arg = STARK_LAB_EVT_*.
 *
 * Any task may call; one mutex serialises, and nothing is allocated after init.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_board.h"
#include "stark_err.h"

#define STARK_SYS_LAB      8u

#define STARK_LAB_EVT_PINS 1 /* the offer changed, or a held pin was withdrawn */

typedef enum { STARK_LAB_PULL_NONE, STARK_LAB_PULL_UP, STARK_LAB_PULL_DOWN } stark_lab_pull_t;

/* What a pad is doing, found with each pull applied for 2 ms. */
typedef enum {
    STARK_LAB_PIN_FLOATING, /* follows the pull: nothing drives it */
    STARK_LAB_PIN_HIGH,     /* driven high against the pull-down */
    STARK_LAB_PIN_LOW       /* driven low against the pull-up */
} stark_lab_pinstate_t;

/* What a held pin is doing now, as the service last set it. */
typedef enum {
    STARK_LAB_MODE_HIZ, /* input, no pull */
    STARK_LAB_MODE_IN,  /* input with a pull (stark_lab_info_t.pull) */
    STARK_LAB_MODE_OUT, /* driven to stark_lab_info_t.level */
    STARK_LAB_MODE_PWM  /* the PWM output: hz and duty */
} stark_lab_mode_t;

typedef struct {
    stark_lab_mode_t mode;
    stark_lab_pull_t pull; /* IN */
    bool level;            /* OUT: the level it was driven to and read back at */
    uint32_t hz;           /* PWM */
    uint32_t duty_pct;     /* PWM */
    bool armed;
} stark_lab_info_t;

typedef struct {
    int pin;
    const char *name; /* "LAB_UART_TX", "MOD0_CS", ... */
    stark_pin_class_t cls;
    bool offered; /* as of now: LAB, or SLOT with its slot reading ABSENT */
    bool held;
} stark_lab_pin_t;

/*
 * Opens the transport and logs `lab: pins <offered pins>` (`lab: pins` then one number per pin)
 * from the module table as it is: call after stark_module_init() has read it. Creates no task
 * and subscribes to no event (the composition root calls stark_lab_refresh()). Never
 * boot-critical. STARK_ERR_STATE when already initialised.
 */
stark_err_t stark_lab_init(void);

/* The whole offer table, with the offered and held flags as of now; returns how many exist. */
size_t stark_lab_pins(stark_lab_pin_t *out, size_t max);

/*
 * Takes a pin for `owner` (a short name, copied) and releases it to hi-Z first.
 * STARK_ERR_NOT_SUPPORTED when it is not offered, STARK_ERR_BUSY when held,
 * STARK_ERR_INVALID_ARG without an owner.
 */
stark_err_t stark_lab_acquire(int pin, const char *owner);

/* Hi-Z, no pull, arm cleared. Any task; a no-op for a pin that is not held. */
void stark_lab_release(int pin);
void stark_lab_release_all(void);

/*
 * The arm: the app calls it after the user pressed OK on a dialog naming the pin and
 * "connected hardware may be damaged". Per pin, RAM only; cleared by release, withdrawal, a
 * failed drive and leaving the app. STARK_ERR_STATE when the pin is not held.
 */
stark_err_t stark_lab_arm(int pin);

/* Input with an internal pull (about 45 kOhm). STARK_ERR_STATE when not held. */
stark_err_t stark_lab_input(int pin, stark_lab_pull_t pull);

/* Input, no pull, the pad released but the pin kept: held, armed and mode HIZ. STARK_ERR_STATE
 * when not held. */
stark_err_t stark_lab_hiz(int pin);

/* The level on the pad. STARK_ERR_STATE when not held. */
stark_err_t stark_lab_read(int pin, bool *level);

/*
 * What the pad is doing: each pull for 2 ms, then no pull. The pin is left an input. A pad
 * that reads high with the pull-down is driven high, one that reads low with the pull-up is
 * driven low, one that follows the pulls is floating; any other result is STARK_ERR_IO (a
 * pad that moved while it was looked at).
 */
stark_err_t stark_lab_state(int pin, stark_lab_pinstate_t *st);

/*
 * Drives the pad at the weakest strength. STARK_ERR_STATE unless held and armed. The pad is
 * read back after 1 ms; a mismatch (contention) puts the pin in hi-Z, clears the arm, logs
 * `lab: contention pin <n>` and returns STARK_ERR_IO. A transport failure does the same.
 */
stark_err_t stark_lab_write(int pin, bool level);

/*
 * What the pin is doing now (the last thing the service did to it). STARK_ERR_NOT_SUPPORTED when
 * it is not offered, STARK_ERR_STATE when not held. Drives nothing.
 */
stark_err_t stark_lab_info(int pin, stark_lab_info_t *out);

/*
 * Flips an output driven through stark_lab_write(): STARK_ERR_STATE unless the pin is held,
 * armed and currently an output. Otherwise as stark_lab_write() (read-back, contention).
 */
stark_err_t stark_lab_toggle(int pin);

/*
 * PWM on the pin (STARK-0702): `hz` 100 to 40 000, `duty_pct` 0 to 100, else
 * STARK_ERR_INVALID_ARG; STARK_ERR_STATE unless held and armed; STARK_ERR_BUSY while another pin
 * has the PWM (the same pin may be started again with new values). Weakest drive strength, LEDC
 * channel 2 only. The output is not read back (a square wave has no one level): the arm's
 * dialog is the guard. A failure puts the pin in hi-Z and clears the arm.
 */
stark_err_t stark_lab_pwm_start(int pin, uint32_t hz, uint32_t duty_pct);

/* Stops the PWM and releases its pad to hi-Z; the pin stays held and armed. A no-op without one. */
void stark_lab_pwm_stop(void);

/*
 * One pulse (STARK-0702): `level` for `us` (10, 100, 1000, 10 000, 100 000 or 1 000 000, else
 * STARK_ERR_INVALID_ARG), STARK_ERR_STATE unless held and armed. The pad is first driven to the
 * opposite level and read back (contention is detected there and ends in hi-Z, as for a write), the
 * pulse follows, and the pad returns to the mode it had. Blocks the caller for the width: call it
 * from the app worker. A pulse is one pulse; nothing repeats.
 */
stark_err_t stark_lab_pulse(int pin, bool level, uint32_t us);

/* Ends a pulse that is waiting, early (within 10 ms). Any task, takes no lock. */
void stark_lab_pulse_cancel(void);

/* ---- the GPIO app's steps (lab_pwm.c, pure; STARK-0702) ------------------------------------ */

/*
 * One step of the frequency row: the leading digit moves by one unit of its decade (100, 200, ...
 * 900, 1000, 2000, ... 9000, 10000, 20000, ... 40000), so every decade is crossed. A value off
 * that ladder moves to the next rung in the direction. Clamped to the limits: at an edge it
 * returns `hz` unchanged. dir is the sign of the step (zero leaves it unchanged).
 */
uint32_t stark_lab_pwm_step_hz(uint32_t hz, int dir);

/* One step of the duty row: 1 %, clamped to 0..100; unchanged at an edge. */
uint32_t stark_lab_pwm_step_duty(uint32_t duty_pct, int dir);

/* The next width in the direction, clamped to the list; an unlisted value gives 100 us. */
uint32_t stark_lab_pulse_step(uint32_t us, int dir);

/*
 * Recomputes the offer: a held pin that is no longer offered goes to hi-Z at once, loses its
 * owner and its arm, and is logged as `lab: pin <n> withdrawn (slot <s>)`; the offer is
 * logged again as `lab: pins ...` when it changed, and STARK_LAB_EVT_PINS is published.
 * The composition root calls it on STARK_SYS_MODULE; every call above also checks the live
 * table, so a pin is never used in the gap.
 */
void stark_lab_refresh(void);
