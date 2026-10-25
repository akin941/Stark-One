/*
 * stark_nfc.h — L3 NFC service (TASKS.md STARK-0402…0405, ADR-0020):
 * a PN532 on the shared I2C bus, reached through stark_nfc_transport. The
 * pure core (ndef.h, pn532.c and friends) makes every protocol decision.
 *
 * Module state changes and tag events are published as STARK_EVT_SYSTEM /
 * STARK_SYS_NFC with system.arg = one of STARK_NFC_EVT_*.
 */
#pragma once

#include "stark_err.h"

#define STARK_SYS_NFC              5u

#define STARK_NFC_EVT_TAG          1 /* a tag was read */
#define STARK_NFC_EVT_LEFT         2 /* the tag left the field */
#define STARK_NFC_EVT_MODULE       3 /* stark_nfc_module() changed */
#define STARK_NFC_EVT_WRITE_DONE   4
#define STARK_NFC_EVT_WRITE_FAILED 5
#define STARK_NFC_EVT_DUMP_DONE    6
#define STARK_NFC_EVT_DUMP_FAILED  7

typedef enum {
    STARK_NFC_NOT_FITTED, /* the board has no I2C pins */
    STARK_NFC_ABSENT,     /* nothing (or not a PN532) answers at 0x24 */
    STARK_NFC_READY,
    STARK_NFC_LOST /* answered at boot, stopped answering (STARK-0403) */
} stark_nfc_module_t;

/*
 * Opens the transport and detects the module (GetFirmwareVersion,
 * SAMConfiguration, one passive retry, field off). Logs `nfc: pn532 fw
 * <ver>.<rev>`, `nfc: no module (<err>)`, `nfc: unexpected chip 0x<ic>` or
 * `nfc: not fitted`. A missing module is not an error: returns STARK_OK and
 * stark_nfc_module() says what was found.
 */
stark_err_t stark_nfc_init(void);
stark_nfc_module_t stark_nfc_module(void);
