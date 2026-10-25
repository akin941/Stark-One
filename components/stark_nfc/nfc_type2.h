/*
 * nfc_type2.h — private to stark_nfc: NFC Forum Type 2 tag memory (TASKS.md
 * STARK-0403/0404; Type 2 Tag specification §2-§4). Pure.
 *
 * Pages are 4 bytes. Pages 0-2 hold the UID and lock bytes, page 3 the
 * capability container (CC), and the data area starts at page 4. Inside it,
 * TLV blocks: NULL 00, Lock Control 01, Memory Control 02, NDEF 03,
 * Terminator FE; a length is 1 byte, or FF and 2 bytes.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

#define NFC_T2_DATA_PAGE 4u
#define NFC_T2_READ      0x30u /* 4 pages */
#define NFC_T2_WRITE     0xA2u /* 1 page */
#define NFC_T2_VERSION   0x60u

typedef struct {
    bool valid;          /* E1, version 1.x, a size, read access granted */
    bool writable;       /* write access 0 */
    uint16_t data_bytes; /* size byte x 8 */
} nfc_t2_cc_t;

nfc_t2_cc_t nfc_t2_cc(const uint8_t cc[4]);

/* The largest NDEF message a data area of data_bytes holds with its TLV
 * header and terminator. */
uint16_t nfc_t2_ndef_capacity(uint16_t data_bytes);

typedef enum {
    NFC_TLV_FOUND, /* the NDEF TLV's value starts at *off, *len bytes long */
    NFC_TLV_NONE,  /* a terminator, or the end, before any NDEF TLV */
    NFC_TLV_MORE,  /* *need bytes of the data area are required to go on */
    NFC_TLV_BAD    /* a length past the data area */
} nfc_tlv_t;

/* Walks the TLVs of a data area of `total` bytes, of which the first `avail`
 * are in area. */
nfc_tlv_t nfc_t2_tlv(const uint8_t *area, size_t avail, size_t total, size_t *off, size_t *len,
                     size_t *need);

/* The NDEF TLV for a message of len bytes, followed by a Terminator and
 * zero-padded to whole pages (STARK-0404): its size in bytes, 0 when len is
 * over 0xFFFE. */
size_t nfc_t2_tlv_size(size_t len);

/* Page `page` (0-based within that TLV) of it, built on the fly so the
 * writer never holds a second copy of the message. */
void nfc_t2_tlv_page(const uint8_t *msg, size_t len, size_t page, uint8_t out[4]);
