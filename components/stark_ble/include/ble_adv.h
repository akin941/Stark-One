/*
 * ble_adv.h — Bluetooth LE advertising-channel PDU dissection (TASKS.md
 * STARK-1002, ADR-0027): classify an advertising PDU and read the AD
 * structures we analyse (Flags, local name, 16-bit service UUIDs,
 * manufacturer-specific data). Pure; host-tested against spec-derived vectors.
 *
 * The AdvA/TargetA and the AD-structure values are views into the caller's
 * buffer: nothing is copied and nothing is allocated, as in dot11_frame.h /
 * ndef.h. Every length is checked against the buffer before it is used. This
 * is receive-side observation only — there is no advertiser/transmit
 * counterpart anywhere (ADR-0027 §3). Advertising data is public by design;
 * no bonding key or private pairing material is read (SECURITY_SCOPE §2).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

/* Advertising PDU types (header bits 0-3). Others pass through as the raw value. */
#define BLE_PDU_ADV_IND          0x0u
#define BLE_PDU_ADV_DIRECT_IND   0x1u
#define BLE_PDU_ADV_NONCONN_IND  0x2u
#define BLE_PDU_SCAN_REQ         0x3u
#define BLE_PDU_SCAN_RSP         0x4u
#define BLE_PDU_CONNECT_IND      0x5u
#define BLE_PDU_ADV_SCAN_IND     0x6u

/* AD structure types (Bluetooth Assigned Numbers) we surface. */
#define BLE_AD_FLAGS             0x01u
#define BLE_AD_UUID16_INCOMPLETE 0x02u
#define BLE_AD_UUID16_COMPLETE   0x03u
#define BLE_AD_NAME_SHORT        0x08u
#define BLE_AD_NAME_COMPLETE     0x09u
#define BLE_AD_MANUFACTURER      0xFFu

#define BLE_ADDR_LEN             6u

/*
 * A parsed advertising PDU. adv_a/target_a are 6-octet views, or NULL when not
 * present for the PDU type. The AD-structure fields are filled only for the
 * types that carry an AdvData section (ADV_IND / ADV_NONCONN_IND / SCAN_RSP /
 * ADV_SCAN_IND); name is NULL when no local-name AD structure is present.
 */
typedef struct {
    uint8_t pdu_type; /* raw 4-bit PDU type */
    bool tx_add;      /* TxAdd: advertiser address is random (true) vs public */
    bool rx_add;      /* RxAdd */
    bool ch_sel;      /* Channel Selection Algorithm #2 supported */
    uint8_t length;   /* payload length from the header */

    const uint8_t *adv_a;    /* advertiser address, view; NULL if absent */
    const uint8_t *target_a; /* ADV_DIRECT_IND target address, view; else NULL */

    bool has_flags; /* a Flags AD structure was present */
    uint8_t flags;  /* its first octet */

    const uint8_t *name; /* local-name value, view; NULL if absent */
    uint8_t name_len;
    bool name_complete; /* true: complete (0x09); false: shortened (0x08) */

    const uint8_t *uuid16; /* 16-bit service UUID list, view (little-endian pairs) */
    uint8_t uuid16_len;    /* octets (2 per UUID); 0 if absent */
    bool uuid16_complete;  /* true: complete list (0x03); false: incomplete (0x02) */

    const uint8_t *mfg; /* manufacturer-specific data, view (company id first, LE) */
    uint8_t mfg_len;    /* octets; 0 if absent */
} ble_adv_t;

/*
 * Parse buf[0..len) (one advertising-channel PDU: 2-octet header + payload)
 * into out. STARK_OK on a well-formed PDU; the header, the addresses present
 * for the type, and the AD structures are filled. A buffer too short for the
 * header, a payload length that overruns the buffer, or a malformed AD section
 * returns STARK_ERR_INVALID_ARG; no read ever crosses buf+len. out must be
 * non-NULL.
 */
stark_err_t ble_adv_parse(const uint8_t *buf, size_t len, ble_adv_t *out);

/*
 * Walk an AD-structure section buf[0..len) and return the first structure whose
 * AD type is `type`: its data view in *val and data length in *val_len (the
 * structure length minus the type octet). true when found. Bounds-checked: a
 * structure whose declared length runs past the section end, or a zero length
 * (padding / early termination), ends the walk. val/val_len must be non-NULL.
 */
bool ble_adv_find_ad(const uint8_t *buf, size_t len, uint8_t type, const uint8_t **val,
                     uint8_t *val_len);
