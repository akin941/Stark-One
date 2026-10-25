/*
 * nfc_types.h — NFC types shared by every part of stark_nfc (TASKS.md V0.4
 * "Common definitions", ADR-0020). Pure.
 */
#pragma once

#include <stdint.h>

#define NFC_UID_MAX       10 /* ISO 14443A: 4, 7 or 10 bytes */
#define NFC_T2_PAGE_BYTES 4
#define NFC_T2_MAX_PAGES  231 /* NTAG216: pages 0..230 */
#define NFC_NDEF_MAX      872 /* the largest Type 2 data area supported: NTAG216, CC size 0x6D x 8 */

typedef enum {
    NFC_TECH_UNKNOWN = 0,
    NFC_TECH_NTAG213,
    NFC_TECH_NTAG215,
    NFC_TECH_NTAG216,
    NFC_TECH_ULTRALIGHT,
    NFC_TECH_TYPE2, /* another Type 2 tag */
    NFC_TECH_CLASSIC_MINI,
    NFC_TECH_CLASSIC_1K,
    NFC_TECH_CLASSIC_4K,
    NFC_TECH_ISO_DEP,
    NFC_TECH_COUNT
} nfc_tech_t;

/* "NTAG213", "CLASSIC_1K", ... — the spelling used in logs, dumps and the app
 * (nfc_ident.c). */
const char *nfc_tech_name(nfc_tech_t tech);

typedef struct {
    uint8_t uid[NFC_UID_MAX];
    uint8_t uid_len;
    uint16_t atqa; /* SENS_RES, as sent (MSB first) */
    uint8_t sak;   /* SEL_RES */
    nfc_tech_t tech;
    uint16_t pages; /* Type 2: total pages; 0 otherwise */
} nfc_tag_t;
