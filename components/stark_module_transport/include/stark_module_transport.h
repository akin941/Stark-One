/*
 * stark_module_transport.h — L1, read-only access to module ID EEPROMs on the
 * shared I2C bus (TASKS.md STARK-0502). Read-only by construction: there is no
 * write call (ADR-0021 §2). One caller, the module task.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

/* STARK_ERR_NOT_SUPPORTED when the board has no I2C bus. */
stark_err_t stark_module_transport_open(void);

/* An address-only check: STARK_OK on an ACK, STARK_ERR_NOT_FOUND on a NACK. */
stark_err_t stark_module_transport_probe(uint8_t addr, uint32_t timeout_ms);

/* A word-address write, then a read of len bytes. */
stark_err_t stark_module_transport_read(uint8_t addr, uint8_t off, uint8_t *buf, size_t len,
                                        uint32_t timeout_ms);
