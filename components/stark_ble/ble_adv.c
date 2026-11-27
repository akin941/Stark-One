/*
 * ble_adv.c — Bluetooth LE advertising-channel PDU dissection (STARK-1002,
 * ADR-0027). Pure, view-based, bounds-checked. Receive-side only: this file
 * classifies and reads public advertising data; it builds nothing, and reads
 * no pairing/bonding material (SECURITY_SCOPE §2).
 */
#include "ble_adv.h"

#include <string.h>

#define BLE_HDR_LEN 2u /* PDU header: type/flags octet + length octet */

bool ble_adv_find_ad(const uint8_t *buf, size_t len, uint8_t type, const uint8_t **val,
                     uint8_t *val_len)
{
    if (buf == NULL || val == NULL || val_len == NULL) {
        return false;
    }
    size_t off = 0u;
    while (off < len) {
        uint8_t ad_len = buf[off];
        if (ad_len == 0u) {
            return false; /* zero length: padding / early termination, end of walk */
        }
        if (off + 1u + (size_t)ad_len > len) {
            return false; /* structure runs past the section: stop, never over-read */
        }
        uint8_t ad_type = buf[off + 1u];
        if (ad_type == type) {
            *val = buf + off + 2u;
            *val_len = (uint8_t)(ad_len - 1u); /* drop the type octet */
            return true;
        }
        off += 1u + (size_t)ad_len;
    }
    return false;
}

/* Fill the AD-structure fields from an AdvData section. */
static void ble_adv_parse_addata(const uint8_t *ad, size_t ad_len, ble_adv_t *out)
{
    const uint8_t *val = NULL;
    uint8_t val_len = 0u;
    if (ble_adv_find_ad(ad, ad_len, BLE_AD_FLAGS, &val, &val_len) && val_len >= 1u) {
        out->has_flags = true;
        out->flags = val[0];
    }
    if (ble_adv_find_ad(ad, ad_len, BLE_AD_NAME_COMPLETE, &val, &val_len)) {
        out->name = (val_len > 0u) ? val : NULL;
        out->name_len = val_len;
        out->name_complete = true;
    } else if (ble_adv_find_ad(ad, ad_len, BLE_AD_NAME_SHORT, &val, &val_len)) {
        out->name = (val_len > 0u) ? val : NULL;
        out->name_len = val_len;
        out->name_complete = false;
    }
    if (ble_adv_find_ad(ad, ad_len, BLE_AD_UUID16_COMPLETE, &val, &val_len) && val_len > 0u) {
        out->uuid16 = val;
        out->uuid16_len = val_len;
        out->uuid16_complete = true;
    } else if (ble_adv_find_ad(ad, ad_len, BLE_AD_UUID16_INCOMPLETE, &val, &val_len) &&
               val_len > 0u) {
        out->uuid16 = val;
        out->uuid16_len = val_len;
        out->uuid16_complete = false;
    }
    if (ble_adv_find_ad(ad, ad_len, BLE_AD_MANUFACTURER, &val, &val_len) && val_len > 0u) {
        out->mfg = val;
        out->mfg_len = val_len;
    }
}

stark_err_t ble_adv_parse(const uint8_t *buf, size_t len, ble_adv_t *out)
{
    if (buf == NULL || out == NULL || len < BLE_HDR_LEN) {
        return STARK_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    uint8_t hdr = buf[0];
    out->pdu_type = (uint8_t)(hdr & 0x0Fu);
    out->ch_sel = (hdr & (1u << 5)) != 0u;
    out->tx_add = (hdr & (1u << 6)) != 0u;
    out->rx_add = (hdr & (1u << 7)) != 0u;
    out->length = buf[1];

    if ((size_t)out->length + BLE_HDR_LEN > len) {
        return STARK_ERR_INVALID_ARG; /* header claims more payload than the buffer holds */
    }

    const uint8_t *payload = buf + BLE_HDR_LEN;
    size_t plen = out->length;

    /* ADV_DIRECT_IND carries AdvA + TargetA and no AD structures. */
    if (out->pdu_type == BLE_PDU_ADV_DIRECT_IND) {
        if (plen >= BLE_ADDR_LEN) {
            out->adv_a = payload;
        }
        if (plen >= 2u * BLE_ADDR_LEN) {
            out->target_a = payload + BLE_ADDR_LEN;
        }
        return STARK_OK;
    }

    /* The advertising PDUs that carry AdvA followed by an AdvData section. */
    bool has_advdata =
        (out->pdu_type == BLE_PDU_ADV_IND || out->pdu_type == BLE_PDU_ADV_NONCONN_IND ||
         out->pdu_type == BLE_PDU_SCAN_RSP || out->pdu_type == BLE_PDU_ADV_SCAN_IND);
    if (has_advdata && plen >= BLE_ADDR_LEN) {
        out->adv_a = payload;
        ble_adv_parse_addata(payload + BLE_ADDR_LEN, plen - BLE_ADDR_LEN, out);
    }
    return STARK_OK;
}
