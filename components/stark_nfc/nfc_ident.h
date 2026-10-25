/*
 * nfc_ident.h — private to stark_nfc: tag technology from ATQA/SAK and a
 * Type 2 tag's GET_VERSION reply (TASKS.md STARK-0403; NXP AN10833 and the
 * NTAG21x / MF0UL data sheets). Pure.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "nfc_types.h"

/* version: the 8-byte GET_VERSION reply, or NULL when the tag gave none. */
nfc_tech_t nfc_ident(uint16_t atqa, uint8_t sak, const uint8_t *version);

/* Total pages of a known Type 2 product (NTAG213 45, 215 135, 216 231);
 * 0 when the capability container has to say. */
uint16_t nfc_tech_pages(nfc_tech_t tech);

/* True for the technologies whose memory is read as Type 2 pages. */
bool nfc_tech_is_type2(nfc_tech_t tech);
