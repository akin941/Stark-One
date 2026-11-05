/*
 * module_desc.h — the ID EEPROM descriptor (TASKS.md STARK-0501,
 * docs/module-format.md). Pure. Input is untrusted: any content parses to
 * validated fields or is rejected with a reason. The firmware has no encoder
 * and no way to write an EEPROM.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MODULE_DESC_SIZE 32
#define MODULE_NAME_MAX  18

typedef struct {
    uint16_t id;
    uint8_t rev;
    uint32_t caps;
    char name[MODULE_NAME_MAX + 1]; /* printable ASCII, NUL-terminated */
} module_desc_t;

typedef enum {
    MODULE_DESC_OK = 0,
    MODULE_DESC_BLANK, /* all 0xFF: an erased chip */
    MODULE_DESC_BAD_MAGIC,
    MODULE_DESC_BAD_VERSION, /* a format this firmware does not know */
    MODULE_DESC_BAD_CRC,
    MODULE_DESC_BAD_FIELD, /* reserved bytes, module ID or name */
    MODULE_DESC_TRUNCATED,
} module_desc_result_t;

/* *out is written only on MODULE_DESC_OK. */
module_desc_result_t module_desc_parse(const uint8_t *raw, size_t len, module_desc_t *out);

/* False for a combination this hardware cannot have (TASKS.md V0.5): the IR
 * bits on a slot (IR is onboard), or NFC over SPI (our NFC path is I2C). */
bool module_desc_caps_supported(uint32_t caps);

/* "ok", "blank", "bad magic", "bad version", "bad crc", "bad field", "truncated". */
const char *module_desc_result_str(module_desc_result_t r);
