/*
 * stark_lab_transport.h — the lab tools' wires (TASKS.md STARK-0701, ADR-0023 §2): the one
 * seam between the lab service and the hardware, and the fifth and last substitution of the
 * emulator test image. Today it carries the pad group (configure, read, hi-Z); later V0.7
 * tasks append the PWM and edge capture, the shared I2C bus, SPI on a leased slot and the
 * lab UART.
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

#define STARK_LAB_PAD_NONE 0u
#define STARK_LAB_PAD_UP   1u
#define STARK_LAB_PAD_DOWN 2u

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
