/*
 * ui_port_gpio.h — host UI test port: the lab service behind the GPIO app (TASKS.md STARK-0702).
 * It is stark_lab.h implemented over the real pad decisions (components/stark_lab/lab_pins.c)
 * and the test-only pad model (test/fake/fake_lab.c), with the module table of ui_port_module.h
 * deciding the slot pins, so the app is tested against the rules it ships with and the pads it
 * leaves can be inspected (fake_lab_mode(), fake_lab_pull()). No lock: the host has one thread.
 */
#pragma once

#include <stdbool.h>
#include "stark_board.h"
#include "stark_module.h"

/* Power-on: every pad hi-Z, slot 0 reads ABSENT (the three slot pins are on offer), no wires. */
void ui_port_gpio_reset(void);

/* The same with another pin map (NULL: the board's): a map that wires no line offers no pin. */
void ui_port_gpio_reset_with(const stark_board_pins_t *map);

/* Slot 0 changes state and the module service says so (STARK_SYS_MODULE is published). */
void ui_port_gpio_set_slot0(stark_module_state_t state);

/* Pulses the service was asked to cut short, and the widths of the pulses it ran. */
unsigned ui_port_gpio_cancels(void);

/* Measurements the service was asked to cut short (stark_lab_measure_cancel()). */
unsigned ui_port_gpio_measure_cancels(void);
