/*
 * stark_nfc.h — L3 NFC service (TASKS.md STARK-0402…0405, ADR-0020):
 * a PN532 on the shared I2C bus, reached through stark_nfc_transport. The
 * pure core (ndef.h, pn532.c and friends) makes every protocol decision.
 *
 * Module state changes and tag events are published as STARK_EVT_SYSTEM /
 * STARK_SYS_NFC with system.arg = one of STARK_NFC_EVT_*.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "nfc_dump.h"
#include "nfc_types.h"
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

/* What reading the present tag gave (STARK-0403). */
typedef struct {
    nfc_tag_t tag;
    stark_err_t read_err;   /* STARK_OK, or why the NDEF read stopped */
    bool ndef_present;      /* Type 2 with a valid CC and an NDEF TLV */
    bool writable;          /* CC write access 0 */
    uint16_t ndef_capacity; /* the largest NDEF message the CC allows */
    uint16_t ndef_len;
    uint8_t ndef[NFC_NDEF_MAX];
} stark_nfc_read_t;

/*
 * Polling, receive-first (ADR-0020 §5): start turns the RF field on and polls
 * every CONFIG_STARK_NFC_POLL_MS; stop turns it off and forgets the tag. Both
 * return at once (the NFC task carries them out). STARK_ERR_NOT_SUPPORTED
 * without a ready module.
 */
stark_err_t stark_nfc_start(void);
stark_err_t stark_nfc_stop(void);

/* A copy of the present tag's snapshot; STARK_ERR_NOT_FOUND when none. */
stark_err_t stark_nfc_take(stark_nfc_read_t *out);

/*
 * Writes msg as the NDEF message of the tag whose UID is given — only that
 * tag — then reads it back (STARK-0404). Returns at once; STARK_SYS_NFC
 * WRITE_DONE or WRITE_FAILED follows, then the tag is read again (TAG).
 * STARK_ERR_STATE while not polling, STARK_ERR_BUSY while a write is pending.
 * Callers confirm with the user first (SECURITY_SCOPE §2, ADR-0020 §5).
 */
stark_err_t stark_nfc_write_ndef(const uint8_t *uid, uint8_t uid_len, const uint8_t *msg,
                                 size_t len);
/* The last write's outcome (nfc_writer.h lists the errors). */
stark_err_t stark_nfc_write_result(void);

/*
 * Dumps the present tag into *out — the caller's buffer, untouched until the
 * event (STARK-0405): selected again (same UID), every Type 2 page read; an
 * identification-only tag dumps no pages. Returns at once; STARK_SYS_NFC
 * DUMP_DONE or DUMP_FAILED follows. STARK_ERR_STATE while not polling or
 * without a tag, STARK_ERR_BUSY while a dump or write is pending.
 */
stark_err_t stark_nfc_request_dump(nfc_dump_t *out);
stark_err_t stark_nfc_dump_result(void);
