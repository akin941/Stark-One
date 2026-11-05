/*
 * fake_24c02.h — test-only 24C02 EEPROMs as seen over I2C (TASKS.md
 * STARK-0501, ADR-0021 §6). Pure C, linked by the host tests and by the
 * emulator test image's substitute stark_module_transport; never by the
 * production firmware.
 *
 * It answers transactions: an address ACK or NACK per device, and a word
 * address pointer that a write sets and a read advances, wrapping at 256.
 * Electrical behaviour is not modelled.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

#define FAKE_24C02_SIZE    256
#define FAKE_24C02_DEVICES 8 /* 0x50..0x57: the chip's A0-A2 straps */

typedef enum {
    FAKE_24C02_NONE = 0,
    FAKE_24C02_NACK_ADDR, /* the address is not acknowledged */
    FAKE_24C02_NACK_DATA, /* the address is, then the data phase fails */
    FAKE_24C02_TIMEOUT,
    FAKE_24C02_BUS_ERROR,
    FAKE_24C02_TRUNCATE, /* removed mid-read: a data phase fails after its first byte */
    FAKE_24C02_BIT_FLIP, /* a read returns one flipped bit */
    FAKE_24C02_FAULT_COUNT
} fake_24c02_fault_t;

void fake_24c02_reset(void);

/* Fits a chip at addr with image at offset 0 and the rest erased. */
void fake_24c02_put(uint8_t addr, const uint8_t *image, size_t len);
void fake_24c02_remove(uint8_t addr);

/* The next count transactions fail this way; -1 until cleared, 0 clears. */
void fake_24c02_fault(fake_24c02_fault_t kind, int count);

/* The transactions a master can make. */
stark_err_t fake_24c02_probe(uint8_t addr);
stark_err_t fake_24c02_write(uint8_t addr, const uint8_t *buf,
                             size_t len); /* buf[0]: word address */
stark_err_t fake_24c02_read(uint8_t addr, uint8_t *buf, size_t len);
stark_err_t fake_24c02_read_at(uint8_t addr, uint8_t off, uint8_t *buf, size_t len);

/* Transactions since reset: bounded-work assertions. */
unsigned fake_24c02_transactions(void);
