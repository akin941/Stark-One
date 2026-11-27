/*
 * dot11_frame.c — IEEE 802.11 MAC frame dissection (STARK-1001, ADR-0027).
 * Pure, view-based, bounds-checked. Receive-side only: this file classifies
 * and reads tagged parameters; it builds nothing. The data body is never
 * parsed, so no key/EAPOL/payload bytes are exposed (SECURITY_SCOPE §2).
 */
#include "dot11_frame.h"

#include <string.h>

/* MAC-header field offsets (octets). The header is little-endian. */
#define DOT11_FC_OFF       0u
#define DOT11_ADDR1_OFF    4u
#define DOT11_ADDR2_OFF    10u
#define DOT11_ADDR3_OFF    16u
#define DOT11_SEQ_OFF      22u
#define DOT11_MAC_LEN      6u
#define DOT11_HDR_MIN      24u /* full management/data header incl. sequence control */

/* Body layout for the management frames we read into bss_* fields. */
#define DOT11_FIXED_BEACON 12u /* timestamp(8) + beacon interval(2) + capability(2) */

static uint16_t dot11_rd_u16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

bool dot11_find_ie(const uint8_t *buf, size_t len, uint8_t tag, const uint8_t **val,
                   uint8_t *val_len)
{
    if (buf == NULL || val == NULL || val_len == NULL) {
        return false;
    }
    size_t off = 0u;
    while (off + 2u <= len) {
        uint8_t ie_tag = buf[off];
        uint8_t ie_len = buf[off + 1u];
        if (off + 2u + (size_t)ie_len > len) {
            return false; /* element runs past the section: stop, never over-read */
        }
        if (ie_tag == tag) {
            *val = buf + off + 2u;
            *val_len = ie_len;
            return true;
        }
        off += 2u + (size_t)ie_len;
    }
    return false;
}

/* Fill the bss_* fields from a management frame's tagged-parameter section. */
static stark_err_t dot11_parse_bss(const uint8_t *buf, size_t len, size_t ie_off,
                                   dot11_frame_t *out)
{
    if (len < ie_off) {
        return STARK_OK; /* header classified; no readable tagged section */
    }
    out->has_bss = true;
    const uint8_t *ies = buf + ie_off;
    size_t ies_len = len - ie_off;

    const uint8_t *val = NULL;
    uint8_t val_len = 0u;
    if (dot11_find_ie(ies, ies_len, DOT11_IE_SSID, &val, &val_len)) {
        if (val_len > DOT11_SSID_MAX) {
            return STARK_ERR_INVALID_ARG; /* an SSID element > 32 octets is malformed */
        }
        out->ssid = (val_len > 0u) ? val : NULL;
        out->ssid_len = val_len;
    }
    if (dot11_find_ie(ies, ies_len, DOT11_IE_DS_PARAMS, &val, &val_len) && val_len >= 1u) {
        out->has_channel = true;
        out->channel = val[0];
    }
    if (dot11_find_ie(ies, ies_len, DOT11_IE_SUPP_RATES, &val, &val_len) && val_len > 0u) {
        out->rates = val;
        out->rates_len = val_len;
    }
    return STARK_OK;
}

stark_err_t dot11_parse(const uint8_t *buf, size_t len, dot11_frame_t *out)
{
    if (buf == NULL || out == NULL || len < 4u) {
        return STARK_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    uint16_t fc = dot11_rd_u16le(buf + DOT11_FC_OFF);
    out->version = (uint8_t)(fc & 0x3u);
    out->type = (dot11_type_t)((fc >> 2) & 0x3u);
    out->subtype = (uint8_t)((fc >> 4) & 0xFu);
    out->to_ds = (fc & (1u << 8)) != 0u;
    out->from_ds = (fc & (1u << 9)) != 0u;
    out->retry = (fc & (1u << 11)) != 0u;
    out->protected_frame = (fc & (1u << 14)) != 0u;

    /* Addr1 (RA/DA) is present on every addressed frame; the shortest control
     * frames (ACK/CTS) are exactly this long. */
    if (len >= DOT11_ADDR1_OFF + DOT11_MAC_LEN) {
        out->addr1 = buf + DOT11_ADDR1_OFF;
    }

    if (out->type == DOT11_TYPE_MGMT || out->type == DOT11_TYPE_DATA) {
        if (len < DOT11_HDR_MIN) {
            return STARK_ERR_INVALID_ARG; /* truncated: no full management/data header */
        }
        out->addr2 = buf + DOT11_ADDR2_OFF;
        out->addr3 = buf + DOT11_ADDR3_OFF;
        out->has_seq = true;
        out->seq = (uint16_t)(dot11_rd_u16le(buf + DOT11_SEQ_OFF) >> 4);
    } else if (out->type == DOT11_TYPE_CTRL) {
        /* PS-Poll, RTS, CF-End and CF-End+CF-Ack carry a second address. */
        bool has_addr2 = (out->subtype == 0xAu || out->subtype == 0xBu || out->subtype == 0xEu ||
                          out->subtype == 0xFu);
        if (has_addr2 && len >= DOT11_ADDR2_OFF + DOT11_MAC_LEN) {
            out->addr2 = buf + DOT11_ADDR2_OFF;
        }
    }

    if (out->type == DOT11_TYPE_MGMT) {
        if (out->subtype == DOT11_MGMT_BEACON || out->subtype == DOT11_MGMT_PROBE_RESP) {
            return dot11_parse_bss(buf, len, DOT11_HDR_MIN + DOT11_FIXED_BEACON, out);
        }
        if (out->subtype == DOT11_MGMT_PROBE_REQ) {
            return dot11_parse_bss(buf, len, DOT11_HDR_MIN, out);
        }
    }
    return STARK_OK;
}
