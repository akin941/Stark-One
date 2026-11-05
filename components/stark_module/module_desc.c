/*
 * module_desc.c — the ID EEPROM descriptor (module_desc.h). Pure.
 */
#include "module_desc.h"
#include <string.h>
#include "stark_caps.h"

#define MAGIC_0        0x53u /* "SM" */
#define MAGIC_1        0x4Du
#define FORMAT_VERSION 1u
#define NAME_OFFSET    12u
#define CRC_OFFSET     30u

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF. */
static uint16_t crc16(const uint8_t *b, size_t n)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < n; i++) {
        uint32_t c = crc ^ ((uint32_t)b[i] << 8);
        for (int bit = 0; bit < 8; bit++) {
            c = (c & 0x8000u) != 0 ? (c << 1) ^ 0x1021u : c << 1;
        }
        crc = (uint16_t)c;
    }
    return crc;
}

static bool is_blank(const uint8_t *raw)
{
    for (size_t i = 0; i < MODULE_DESC_SIZE; i++) {
        if (raw[i] != 0xFFu) {
            return false;
        }
    }
    return true;
}

/* 1..MODULE_NAME_MAX printable bytes, no leading space, then only NULs. */
static bool name_valid(const uint8_t *n)
{
    size_t len = 0;
    while (len < MODULE_NAME_MAX && n[len] != 0) {
        if (n[len] < 0x20u || n[len] > 0x7Eu) {
            return false;
        }
        len++;
    }
    for (size_t i = len; i < MODULE_NAME_MAX; i++) {
        if (n[i] != 0) {
            return false;
        }
    }
    return len > 0 && n[0] != ' ';
}

module_desc_result_t module_desc_parse(const uint8_t *raw, size_t len, module_desc_t *out)
{
    if (raw == NULL || out == NULL || len < MODULE_DESC_SIZE) {
        return MODULE_DESC_TRUNCATED;
    }
    if (is_blank(raw)) {
        return MODULE_DESC_BLANK;
    }
    if (raw[0] != MAGIC_0 || raw[1] != MAGIC_1) {
        return MODULE_DESC_BAD_MAGIC;
    }
    if (raw[2] != FORMAT_VERSION) {
        return MODULE_DESC_BAD_VERSION;
    }
    uint16_t want = (uint16_t)(raw[CRC_OFFSET] | (raw[CRC_OFFSET + 1] << 8));
    if (crc16(raw, CRC_OFFSET) != want) {
        return MODULE_DESC_BAD_CRC;
    }
    uint16_t id = (uint16_t)(raw[4] | (raw[5] << 8));
    if (raw[3] != 0 || raw[7] != 0 || id == 0 || id == 0xFFFFu || !name_valid(raw + NAME_OFFSET)) {
        return MODULE_DESC_BAD_FIELD;
    }
    out->id = id;
    out->rev = raw[6];
    out->caps = (uint32_t)raw[8] | ((uint32_t)raw[9] << 8) | ((uint32_t)raw[10] << 16) |
                ((uint32_t)raw[11] << 24);
    memcpy(out->name, raw + NAME_OFFSET, MODULE_NAME_MAX);
    out->name[MODULE_NAME_MAX] = '\0';
    return MODULE_DESC_OK;
}

bool module_desc_caps_supported(uint32_t caps)
{
    if ((caps & (STARK_CAP_IR_TX | STARK_CAP_IR_RX)) != 0) {
        return false;
    }
    return (caps & STARK_CAP_NFC) == 0 || (caps & STARK_CAP_MOD_SPI) == 0;
}

const char *module_desc_result_str(module_desc_result_t r)
{
    switch (r) {
        case MODULE_DESC_OK:
            return "ok";
        case MODULE_DESC_BLANK:
            return "blank";
        case MODULE_DESC_BAD_MAGIC:
            return "bad magic";
        case MODULE_DESC_BAD_VERSION:
            return "bad version";
        case MODULE_DESC_BAD_CRC:
            return "bad crc";
        case MODULE_DESC_BAD_FIELD:
            return "bad field";
        case MODULE_DESC_TRUNCATED:
        default:
            return "truncated";
    }
}
