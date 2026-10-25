/*
 * nfc_ident.c — tag technology (nfc_ident.h). Pure.
 */
#include "nfc_ident.h"
#include <stdbool.h>
#include <stddef.h>

#define VENDOR_NXP       0x04u
#define PRODUCT_ULTRA    0x03u
#define PRODUCT_NTAG     0x04u
#define ATQA_ULTRALIGHT  0x0044u
#define SAK_ISO_DEP_MASK 0x20u

static nfc_tech_t type2(uint16_t atqa, const uint8_t *version)
{
    if (version == NULL) { /* plain Ultralight answers GET_VERSION with a NAK */
        return atqa == ATQA_ULTRALIGHT ? NFC_TECH_ULTRALIGHT : NFC_TECH_TYPE2;
    }
    if (version[1] != VENDOR_NXP) {
        return NFC_TECH_TYPE2;
    }
    if (version[2] == PRODUCT_ULTRA) {
        return NFC_TECH_ULTRALIGHT;
    }
    if (version[2] == PRODUCT_NTAG) {
        switch (version[6]) { /* storage size */
            case 0x0F:
                return NFC_TECH_NTAG213;
            case 0x11:
                return NFC_TECH_NTAG215;
            case 0x13:
                return NFC_TECH_NTAG216;
            default:
                break;
        }
    }
    return NFC_TECH_TYPE2;
}

nfc_tech_t nfc_ident(uint16_t atqa, uint8_t sak, const uint8_t *version)
{
    switch (sak) {
        case 0x00:
            return type2(atqa, version);
        case 0x09:
            return NFC_TECH_CLASSIC_MINI;
        case 0x08:
        case 0x88: /* Infineon's MIFARE Classic 1K */
            return NFC_TECH_CLASSIC_1K;
        case 0x18:
            return NFC_TECH_CLASSIC_4K;
        default:
            return (sak & SAK_ISO_DEP_MASK) != 0 ? NFC_TECH_ISO_DEP : NFC_TECH_UNKNOWN;
    }
}

const char *nfc_tech_name(nfc_tech_t tech)
{
    static const char *const names[NFC_TECH_COUNT] = {
        "UNKNOWN", "NTAG213",      "NTAG215",    "NTAG216",    "ULTRALIGHT",
        "TYPE2",   "CLASSIC_MINI", "CLASSIC_1K", "CLASSIC_4K", "ISO_DEP",
    };
    return (unsigned)tech < NFC_TECH_COUNT ? names[tech] : "UNKNOWN";
}

uint16_t nfc_tech_pages(nfc_tech_t tech)
{
    if (tech == NFC_TECH_NTAG213) {
        return 45;
    }
    if (tech == NFC_TECH_NTAG215) {
        return 135;
    }
    return tech == NFC_TECH_NTAG216 ? 231 : 0;
}

bool nfc_tech_is_type2(nfc_tech_t tech)
{
    return tech >= NFC_TECH_NTAG213 && tech <= NFC_TECH_TYPE2;
}
