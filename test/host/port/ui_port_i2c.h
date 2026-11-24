/*
 * ui_port_i2c.h — host UI test port: the lab service's I2C group behind the I2C app (TASKS.md
 * STARK-0704). It is stark_lab.h's stark_lab_i2c_*() implemented over the real rules
 * (components/stark_lab/lab_i2c.c), the production header's transport guard (the host shim of
 * lab_transport_i2c.c) and the test-only bus (test/fake/fake_lab.c), with a test-only 24C02 behind
 * 0x50 and 0x51 and a PN532 stand-in behind 0x24, so the app is tested against the rules it ships
 * with and the bus it leaves can be inspected (fake_lab_i2c_calls(), fake_lab_i2c_reg()). No lock:
 * the host has one thread. It also supplies stark_board_pins() and stark_module_caps() to the
 * guard.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Power-on: the five lab devices on the bus, no module EEPROM, NFC not confirmed, no fault. */
void ui_port_i2c_reset(void);

/* A module ID EEPROM at 0x50 or 0x51 (filled with 0xA5), or not. */
void ui_port_i2c_eeprom(uint8_t addr, bool present);

/* The NFC capability is confirmed (the PN532 answers at 0x24 and belongs to its driver), or not. */
void ui_port_i2c_nfc(bool confirmed);

/* The clock the arm's age is read from. */
void ui_port_i2c_advance_ms(uint32_t ms);

/* The bus calls the service has made since the reset (a refused write makes none). */
unsigned ui_port_i2c_bus_calls(void);

/* True while an arm that no call has consumed is held (leaving the app must clear it). */
bool ui_port_i2c_armed(void);

/* A crowded bus: every address the five devices and the module EEPROMs do not use acknowledges
 * too, so a sweep finds 112 and the list has more rows than the app shows. */
void ui_port_i2c_crowd(bool on);
