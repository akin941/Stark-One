/*
 * stark_nfc_transport.h — L1: the only code on the I2C bus for NFC
 * (TASKS.md STARK-0402, ADR-0020 §2). The PN532 answers at 7-bit address
 * 0x24 on stark_board_i2c_bus(). stark_nfc's pure driver reaches the chip
 * only through these calls.
 *
 * Errors: an address NACK (nothing there) is STARK_ERR_NOT_FOUND, a bus
 * timeout STARK_ERR_TIMEOUT, anything else STARK_ERR_IO;
 * STARK_ERR_NOT_SUPPORTED when the board has no I2C pins.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

stark_err_t stark_nfc_transport_open(void);
void stark_nfc_transport_close(void);
stark_err_t stark_nfc_transport_write(const uint8_t *buf, size_t len, uint32_t timeout_ms);
stark_err_t stark_nfc_transport_read(uint8_t *buf, size_t len, uint32_t timeout_ms);
