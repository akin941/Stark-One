/*
 * dot11_frame.h — IEEE 802.11 MAC frame dissection (TASKS.md STARK-1001,
 * ADR-0027): classify a captured frame and, for a beacon / probe, read the
 * tagged parameters we analyse (SSID, DS channel, supported rates). Pure;
 * host-tested against spec-derived vectors.
 *
 * Addresses and the SSID/rates are views into the caller's frame buffer:
 * nothing is copied and nothing is allocated, as in ndef.h. Every length is
 * checked against the buffer before it is used. This is receive-side analysis
 * only — there is no frame-building counterpart anywhere (ADR-0027 §3), and no
 * field here carries another network's key/EAPOL/payload bytes: the data body
 * is classified and skipped, never parsed (docs/SECURITY_SCOPE.md §2).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

/* Frame Control type (bits 2-3 of the first octet). */
typedef enum {
    DOT11_TYPE_MGMT = 0,
    DOT11_TYPE_CTRL = 1,
    DOT11_TYPE_DATA = 2,
    DOT11_TYPE_EXT = 3
} dot11_type_t;

/*
 * The management subtypes we name. Others pass through as the raw subtype value
 * in dot11_frame_t.subtype. DEAUTH/DISASSOC are named so the survey can *count*
 * them on receive; naming a subtype never implies the ability to send it — no
 * transmit path exists (ADR-0027 §3).
 */
#define DOT11_MGMT_ASSOC_REQ    0x0u
#define DOT11_MGMT_ASSOC_RESP   0x1u
#define DOT11_MGMT_PROBE_REQ    0x4u
#define DOT11_MGMT_PROBE_RESP   0x5u
#define DOT11_MGMT_BEACON       0x8u
#define DOT11_MGMT_DISASSOC     0xAu
#define DOT11_MGMT_AUTH         0xBu
#define DOT11_MGMT_DEAUTH       0xCu

/* Information-element tag numbers we read. */
#define DOT11_IE_SSID           0u
#define DOT11_IE_SUPP_RATES     1u
#define DOT11_IE_DS_PARAMS      3u
#define DOT11_IE_EXT_SUPP_RATES 50u

#define DOT11_SSID_MAX          32u /* 802.11: an SSID element value is 0..32 octets */

/*
 * A parsed frame. The address pointers are 6-octet views into the caller's
 * buffer, or NULL when that address is not present for the frame's type. The
 * bss_* fields are filled only for a beacon / probe-response / probe-request
 * (has_bss true); ssid is NULL for a hidden/wildcard SSID (ssid_len 0).
 */
typedef struct {
    uint8_t version;      /* protocol version (0 for all current frames) */
    dot11_type_t type;    /* management / control / data / extension */
    uint8_t subtype;      /* raw 4-bit subtype */
    bool to_ds;           /* Frame Control bit 8 */
    bool from_ds;         /* Frame Control bit 9 */
    bool retry;           /* Frame Control bit 11 */
    bool protected_frame; /* Frame Control bit 14 (Protected) */

    const uint8_t *addr1; /* RA / DA — present for every addressed frame */
    const uint8_t *addr2; /* TA / SA — mgmt, data, and some control frames */
    const uint8_t *addr3; /* BSSID / DA / SA — mgmt and data frames */

    bool has_seq; /* sequence control present (mgmt and data) */
    uint16_t seq; /* sequence number 0..4095 (fragment number dropped) */

    bool has_bss;         /* beacon / probe: the tagged section was walked */
    const uint8_t *ssid;  /* SSID element value, view; NULL if hidden/absent */
    uint8_t ssid_len;     /* 0..32 */
    bool has_channel;     /* DS Parameter Set element present */
    uint8_t channel;      /* current channel from the DS Parameter Set */
    const uint8_t *rates; /* Supported Rates element value, view; NULL if absent */
    uint8_t rates_len;    /* 0..255 */
} dot11_frame_t;

/*
 * Parse buf[0..len) into out. STARK_OK on a well-formed frame; the header
 * fields and the addresses present for its type are filled, and for a
 * beacon/probe the tagged parameters are read into the bss_* fields. A buffer
 * too short for the frame's header, or a malformed tagged section, returns
 * STARK_ERR_INVALID_ARG; no read ever crosses buf+len. out must be non-NULL.
 */
stark_err_t dot11_parse(const uint8_t *buf, size_t len, dot11_frame_t *out);

/*
 * Walk a tagged-parameter (information-element) section buf[0..len) and return
 * the first element whose tag is `tag`: its value view in *val and length in
 * *val_len. true when found. Bounds-checked: an element whose declared length
 * runs past the section end is treated as end-of-section (returns false rather
 * than reading over). Shared by dot11_parse and available to callers that want
 * a tag it does not surface. val/val_len must be non-NULL.
 */
bool dot11_find_ie(const uint8_t *buf, size_t len, uint8_t tag, const uint8_t **val,
                   uint8_t *val_len);
