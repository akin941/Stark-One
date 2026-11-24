/*
 * ui_port_spi.h — host UI test port: the lab service's SPI group behind the SPI app (TASKS.md
 * STARK-0706). It is stark_lab.h's stark_lab_spi_*() implemented over the real rules
 * (components/stark_lab/lab_spi.c), the real module core and fake 24C02 (the slot states and the
 * lease), the host shim of the transport's SPI group (the production header's guard) and the
 * test-only SPI devices (test/fake/fake_lab.c), so the app is tested against the rules it ships
 * with and the bus it leaves can be inspected (fake_lab_spi_calls()). No lock: the host has one
 * thread. It also supplies stark_board_pins() and the module table to the guard.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "fake_lab.h"

/* Power-on: no module in either slot, no SPI device, no fault, the clock at zero. */
void ui_port_spi_reset(void);

/* Fits catalogue entry `id` (test/fake/fake_module_catalog.h, 1-based; 0 empties the slot) in a
 * slot and lets the module core settle. */
void ui_port_spi_module(unsigned slot, int id);

/* Another driver holds slot 0's chip select (the send is then BUSY), or lets go of it. */
void ui_port_spi_hold(bool held);

/* The clock the request's age is read from. */
void ui_port_spi_advance_ms(uint32_t ms);

/* Transactions the service has made (SPI_DONE events), and whether a request is still armed. */
unsigned ui_port_spi_dones(void);
bool ui_port_spi_armed(void);

/* The lease claims the service has made, and how many it released: they must end equal. */
unsigned ui_port_spi_claims(void);
unsigned ui_port_spi_releases(void);
