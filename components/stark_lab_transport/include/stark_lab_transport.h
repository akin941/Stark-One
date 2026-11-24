/*
 * stark_lab_transport.h — the lab tools' wires (TASKS.md STARK-0701, ADR-0023 §2): the one
 * seam between the lab service and the hardware, and the fifth and last substitution of the
 * emulator test image. Today it carries the pad group (configure, read, hi-Z), the PWM and
 * pulse outputs (STARK-0702) and the edge capture (STARK-0703); later V0.7 tasks append the
 * shared I2C bus, SPI on a leased slot and the lab UART.
 *
 * Every pad call re-checks the pin against stark_board's classification, whatever the
 * caller says (defence in depth, SECURITY_SCOPE §4.6): a pin that is not offered returns
 * STARK_ERR_NOT_SUPPORTED before any hardware is touched. The one call allowed on a pin
 * that is no longer offered is stark_lab_transport_pad_hiz(), which only releases it.
 * The emulator test image substitutes this component with one built on the test-only
 * fake pads (test/fake/fake_lab.c); the policy below is shared, so neither can touch what
 * it refuses.
 *
 * Callers are serialised by the lab service's mutex; a pad is one owner at a time.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_board.h"
#include "stark_err.h"
#include "stark_module.h"

#define STARK_LAB_PAD_NONE     0u
#define STARK_LAB_PAD_UP       1u
#define STARK_LAB_PAD_DOWN     2u

/* The PWM output (ADR-0023 §4): LEDC channel 2, one pad at a time. */
#define STARK_LAB_PWM_CHANNEL  2u
#define STARK_LAB_PWM_HZ_MIN   100u
#define STARK_LAB_PWM_HZ_MAX   40000u
#define STARK_LAB_PULSE_US_MIN 10u
#define STARK_LAB_PULSE_US_MAX 1000000u

/* Prepares the transport. Idempotent. */
stark_err_t stark_lab_transport_open(void);
void stark_lab_transport_close(void);

/* Input with the pull (STARK_LAB_PAD_*), interrupts off. STARK_ERR_STATE when closed. */
stark_err_t stark_lab_transport_pad_in(int pin, uint8_t pull);

/* Output at the weakest drive strength, the pad readable: weak drive, read-back enabled. */
stark_err_t stark_lab_transport_pad_out(int pin, bool level);

/* The level on the pad, an output's own read-back included. */
stark_err_t stark_lab_transport_pad_read(int pin, bool *level);

/* Input, no pull, interrupts off. Allowed on any pin the board gives a class (not RESERVED). */
stark_err_t stark_lab_transport_pad_hiz(int pin);

/*
 * PWM on a pad at `hz` (STARK_LAB_PWM_HZ_MIN..MAX) and `duty_pct` (0..100), weakest drive
 * strength, on LEDC channel STARK_LAB_PWM_CHANNEL only (the buzzer's and the backlight's channels
 * are never touched). STARK_ERR_INVALID_ARG outside those ranges. A failure leaves the pad in hi-Z.
 */
stark_err_t stark_lab_transport_pwm_start(int pin, uint32_t hz, uint8_t duty_pct);

/* Stops the PWM channel (idle level low) and returns the pad to hi-Z. Allowed on any classified
 * pin. */
stark_err_t stark_lab_transport_pwm_stop(int pin);

/*
 * One pulse: the pad is driven to `level` (weakest strength) for `us` microseconds
 * (STARK_LAB_PULSE_US_MIN..MAX, else STARK_ERR_INVALID_ARG), then to the opposite level, where it
 * stays: the caller restores the mode the pad had. Under 1 ms it is a busy wait, longer ones wait
 * in 10 ms slices at most (so stark_lab_transport_pulse_cancel() takes effect within one), and a
 * cancelled pulse still ends at the opposite level.
 */
stark_err_t stark_lab_transport_pulse(int pin, bool level, uint32_t us);

/* Ends a pulse that is waiting, early. Any task; no lock; a pulse that starts later is not
 * affected. */
void stark_lab_transport_pulse_cancel(void);

/*
 * Edge capture (STARK-0703, ADR-0023 §4): the any-edge interrupt of one offered pad is on for
 * `window_us` (STARK_LAB_EDGES_WINDOW_US_MIN..MAX, else STARK_ERR_INVALID_ARG) and every edge is
 * stamped with the CPU cycle counter into `ts` (the first `cap` are stored; cap 0 only counts).
 * `*n` is how many were stored and `*seen` how many edges came, counted up to cap + 1 (or
 * STARK_LAB_EDGES_GATE_MAX when cap is 0): the capture ends at once when that many have come, so
 * `*seen > cap` is a ring that overflowed. `*first_level` is the level before the first edge. The
 * pad is not configured: it stays what it was, an input with its pull, and the interrupt is
 * removed on every way out. A cancel ends the wait within 10 ms and returns STARK_ERR_STATE.
 * It is an input operation (an interrupt on a pad, never a drive); the service allows it only on
 * a pin it holds as an input. Re-checks the class like every pad call.
 */
#define STARK_LAB_EDGES_WINDOW_US_MIN 10000u
#define STARK_LAB_EDGES_WINDOW_US_MAX 4000000u
#define STARK_LAB_EDGES_GATE_MAX      4096u
stark_err_t stark_lab_transport_pad_edges(int pin, uint32_t window_us, uint32_t *ts, size_t cap,
                                          size_t *n, uint32_t *seen, bool *first_level);

/* Ends a capture that is waiting, early. Any task; no lock; a capture that starts later is not
 * affected. */
void stark_lab_transport_edges_cancel(void);

/* The clock the stamps count at, in hertz. */
uint32_t stark_lab_transport_tick_hz(void);

/*
 * The offer, as a pure rule: a LAB pin always, a SLOT pin only while its slot is empty
 * (reads ABSENT), a RESERVED pin never.
 */
static inline bool stark_lab_pin_offered(stark_pin_class_t cls, bool slot_absent)
{
    return cls == STARK_PIN_LAB || (cls == STARK_PIN_SLOT && slot_absent);
}

/* True when slot `slot` is listed in `list` and reads ABSENT (an unlisted slot: not absent). */
static inline bool stark_lab_slot_absent(const stark_module_entry_t *list, size_t n, int slot)
{
    for (size_t i = 0; i < n; i++) {
        if (list[i].kind == STARK_MODULE_SLOT && (int)list[i].slot == slot) {
            return list[i].state == STARK_MODULE_ABSENT;
        }
    }
    return false;
}

/*
 * The live offer for one GPIO number: stark_board's class and, for a slot's line, the module
 * table as it is now. Shared by the hardware transport and the test one. An empty table (the
 * module service has not started) offers no slot line. Any task.
 */
static inline bool stark_lab_pad_ok(int pin)
{
    int slot = -1;
    stark_pin_class_t cls = stark_board_pin_class(pin, &slot);
    if (cls != STARK_PIN_SLOT) {
        return stark_lab_pin_offered(cls, false);
    }
    stark_module_entry_t list[STARK_MODULE_ENTRIES];
    size_t n = stark_module_list(list, STARK_MODULE_ENTRIES);
    if (n > STARK_MODULE_ENTRIES) {
        n = STARK_MODULE_ENTRIES;
    }
    return stark_lab_pin_offered(cls, stark_lab_slot_absent(list, n, slot));
}

/* The release exemption: any pin the board classifies. */
static inline bool stark_lab_pad_hiz_ok(int pin)
{
    return stark_board_pin_class(pin, NULL) != STARK_PIN_RESERVED;
}
