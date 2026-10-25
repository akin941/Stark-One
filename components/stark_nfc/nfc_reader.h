/*
 * nfc_reader.h — private to stark_nfc: finding and reading a tag through the
 * PN532 (TASKS.md STARK-0403). Pure: it runs on the PN532 driver, so the
 * host tests read every catalogue tag through the driver and the fake.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "nfc_dump.h"
#include "nfc_types.h"
#include "pn532.h"
#include "stark_nfc.h"

#define NFC_EXCHANGE_TIMEOUT_MS 100u

/* InListPassiveTarget, one 106 kbit/s type A target: *found and its UID,
 * ATQA and SAK (tech and pages are left for nfc_reader_read()). */
stark_err_t nfc_reader_poll(pn532_t *d, bool *found, nfc_tag_t *tag);

/* Selects again and checks it is the same tag: STARK_ERR_NOT_FOUND when the
 * field is empty, STARK_ERR_STATE when another tag answers. */
stark_err_t nfc_reader_reselect(pn532_t *d, const nfc_tag_t *expect);

/* InDataExchange with the selected tag: STARK_ERR_NOT_FOUND when it did not
 * answer (it left), STARK_ERR_IO when it refused; cap <= 32. */
stark_err_t nfc_reader_exchange(pn532_t *d, const uint8_t *cmd, size_t n, uint8_t *out, size_t cap,
                                size_t *got);

/* InRelease. */
void nfc_reader_release(pn532_t *d);

/*
 * Reads the tag nfc_reader_poll() found: GET_VERSION for SAK 0x00, the
 * identification, and for Type 2 the CC and the NDEF TLV's value. Never
 * fails as a whole: out->read_err says where it stopped, the identification
 * is kept, and nothing of an earlier read survives. Releases the tag.
 */
void nfc_reader_read(pn532_t *d, const nfc_tag_t *found, stark_nfc_read_t *out);

/*
 * A dump of the tag `tag` describes (STARK-0405): selected again (same UID),
 * then for Type 2 every page 0 .. tag->pages-1 read 4 at a time; an
 * identification-only tag dumps no pages. Errors as nfc_reader_reselect() and
 * nfc_reader_exchange(); out->n_pages counts the pages read either way.
 */
stark_err_t nfc_reader_dump(pn532_t *d, const nfc_tag_t *tag, nfc_dump_t *out);
