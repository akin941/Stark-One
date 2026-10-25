/*
 * nfc_type2.c — Type 2 tag memory (nfc_type2.h). Pure.
 */
#include "nfc_type2.h"

#define CC_MAGIC 0xE1u
#define TLV_NULL 0x00u
#define TLV_NDEF 0x03u
#define TLV_END  0xFEu
#define TLV_LONG 0xFFu

nfc_t2_cc_t nfc_t2_cc(const uint8_t cc[4])
{
    nfc_t2_cc_t r = {0};
    r.valid = cc[0] == CC_MAGIC && (cc[1] >> 4) == 1u && cc[2] != 0 && (cc[3] & 0xF0u) == 0;
    if (r.valid) {
        r.writable = (cc[3] & 0x0Fu) == 0;
        r.data_bytes = (uint16_t)(cc[2] * 8u);
    }
    return r;
}

uint16_t nfc_t2_ndef_capacity(uint16_t data_bytes)
{
    if (data_bytes < 3) {
        return 0;
    }
    return data_bytes - 3u < 0xFFu ? (uint16_t)(data_bytes - 3u) : (uint16_t)(data_bytes - 5u);
}

nfc_tlv_t nfc_t2_tlv(const uint8_t *area, size_t avail, size_t total, size_t *off, size_t *len,
                     size_t *need)
{
    size_t pos = 0;
    while (pos < total) {
        if (pos >= avail) {
            *need = pos + 1;
            return NFC_TLV_MORE;
        }
        uint8_t type = area[pos];
        if (type == TLV_NULL) {
            pos++;
            continue;
        }
        if (type == TLV_END) {
            return NFC_TLV_NONE;
        }
        size_t hdr = 2;
        if (pos + hdr > total) {
            return NFC_TLV_BAD;
        }
        if (pos + hdr > avail) {
            *need = pos + hdr;
            return NFC_TLV_MORE;
        }
        size_t value = area[pos + 1];
        if (value == TLV_LONG) {
            hdr = 4;
            if (pos + hdr > total) {
                return NFC_TLV_BAD;
            }
            if (pos + hdr > avail) {
                *need = pos + hdr;
                return NFC_TLV_MORE;
            }
            value = ((size_t)area[pos + 2] << 8) | area[pos + 3];
        }
        if (pos + hdr + value > total) {
            return NFC_TLV_BAD;
        }
        if (type == TLV_NDEF) {
            *off = pos + hdr;
            *len = value;
            return NFC_TLV_FOUND;
        }
        pos += hdr + value;
    }
    return NFC_TLV_NONE;
}

size_t nfc_t2_tlv_size(size_t len)
{
    if (len > 0xFFFEu) {
        return 0;
    }
    size_t used = (len < TLV_LONG ? 2u : 4u) + len + 1u;
    return (used + 3u) & ~(size_t)3u;
}

/* Byte i of the padded TLV. */
static uint8_t tlv_byte(const uint8_t *msg, size_t len, size_t i)
{
    size_t hdr = len < TLV_LONG ? 2u : 4u;
    if (i == 0) {
        return TLV_NDEF;
    }
    if (i < hdr) {
        if (hdr == 2) {
            return (uint8_t)len;
        }
        return i == 1 ? TLV_LONG : (uint8_t)(i == 2 ? len >> 8 : len);
    }
    if (i < hdr + len) {
        return msg[i - hdr];
    }
    return i == hdr + len ? TLV_END : 0x00;
}

void nfc_t2_tlv_page(const uint8_t *msg, size_t len, size_t page, uint8_t out[4])
{
    for (size_t k = 0; k < 4; k++) {
        out[k] = tlv_byte(msg, len, page * 4u + k);
    }
}
