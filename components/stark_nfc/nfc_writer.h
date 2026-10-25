/*
 * nfc_writer.h — private to stark_nfc: writing an NDEF message to a Type 2
 * tag (TASKS.md STARK-0404, ADR-0020 §5). Pure, over the PN532 driver.
 *
 * Only the tag the user confirmed (same UID), only with a CC that grants
 * write access, only from page 4 and only inside the CC's data area: never
 * the UID, lock bytes, CC, dynamic lock bytes, OTP or configuration pages,
 * so a write can never lock a tag. Every write is read back and compared.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "nfc_types.h"
#include "pn532.h"
#include "stark_err.h"

/*
 * STARK_ERR_NOT_FOUND: no tag; STARK_ERR_STATE: another tag;
 * STARK_ERR_NOT_SUPPORTED: not NDEF-formatted, or read-only;
 * STARK_ERR_NO_SPACE: the message does not fit; STARK_ERR_IO: a write or the
 * read-back failed, or the read-back differs (the tag may hold a partial
 * write — never reported as done). *pages: pages written.
 */
stark_err_t nfc_writer_write(pn532_t *d, const nfc_tag_t *expect, const uint8_t *msg, size_t len,
                             uint16_t *pages);
