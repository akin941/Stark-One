/*
 * stark_module.h — L3 module bus (TASKS.md STARK-0502, ADR-0021): follows the
 * ID EEPROM in each slot and publishes capabilities. A descriptor only
 * declares; a capability counts once a driver confirms it. Changes are
 * published as STARK_EVT_SYSTEM / STARK_SYS_MODULE.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "module_desc.h"
#include "stark_caps.h"
#include "stark_err.h"

#define STARK_SYS_MODULE   6u
#define STARK_MODULE_SLOTS 2u /* HARDWARE.md §6 */

typedef enum {
    STARK_MODULE_ONBOARD, /* soldered to the board: IR */
    STARK_MODULE_BUS,     /* answers on the bus itself, no EEPROM: the PN532 */
    STARK_MODULE_SLOT     /* identified by an ID EEPROM */
} stark_module_kind_t;

typedef enum {
    STARK_MODULE_ABSENT,  /* a slot: "no ID" — the connector has no presence pin (ADR-0021 §4) */
    STARK_MODULE_KNOWN,   /* a valid descriptor, or a confirmed provider */
    STARK_MODULE_GENERIC, /* answered, content unusable: no capabilities */
    STARK_MODULE_CONFLICT /* overlaps another slot: its capabilities are withheld */
} stark_module_state_t;

typedef struct {
    stark_module_kind_t kind;
    stark_module_state_t state;
    uint8_t slot; /* STARK_MODULE_SLOT */
    uint16_t id;
    uint8_t rev;
    uint32_t declared;        /* descriptor bits, unknown ones included */
    uint32_t confirmed;       /* the subset a driver has confirmed */
    module_desc_result_t why; /* GENERIC: why the content was rejected */
    bool unsupported;         /* GENERIC: a valid descriptor, an impossible combination */
    char name[MODULE_NAME_MAX + 1];
} stark_module_entry_t;

/*
 * Reads every slot twice so the first table is settled, logs a line per slot
 * and starts the polling task. Never fails the boot: STARK_OK without an I2C
 * bus too (`module: not fitted`, every slot absent).
 */
stark_err_t stark_module_init(void);

/* Copies up to max entries; returns how many exist. Any task. */
size_t stark_module_list(stark_module_entry_t *out, size_t max);

/* The effective capabilities: confirmed, not withheld, known. Any task. */
uint32_t stark_module_caps(void);

/* A driver's word on capability bits, through the composition root. */
void stark_module_confirm(uint32_t caps, bool present);

/* ---- CS / IRQ arbitration (STARK-0503) ----------------------------------- */

/* A driver's hold on a slot's lines. Removal or replacement of the module
 * invalidates it at once; the owner hears through STARK_SYS_MODULE. */
typedef struct {
    uint8_t slot;
    uint32_t gen;
} stark_module_lease_t;

/*
 * Claims a slot's lines for owner: caps is STARK_CAP_MOD_SPI and/or
 * STARK_CAP_MOD_IRQ. STARK_ERR_INVALID_ARG for anything else,
 * STARK_ERR_NOT_FOUND unless the slot is KNOWN, STARK_ERR_NOT_SUPPORTED if its
 * descriptor does not declare them or the board does not wire them,
 * STARK_ERR_BUSY while another owner holds the slot.
 */
stark_err_t stark_module_claim(uint8_t slot, uint32_t caps, const char *owner,
                               stark_module_lease_t *out);
void stark_module_release(const stark_module_lease_t *l);
bool stark_module_lease_valid(const stark_module_lease_t *l);

/* The claimed lines; -1 for a line not claimed. STARK_ERR_STATE on a stale lease. */
stark_err_t stark_module_lease_pins(const stark_module_lease_t *l, int *cs, int *irq0, int *irq1);

/*
 * A claimed slot's device on the shared SPI2 bus. Transactions go through
 * ESP-IDF's bus lock, so they never interleave with the panel or the SD card
 * (ADR-0018 §4). A stale lease makes every transfer STARK_ERR_STATE; its owner
 * still detaches. Not for apps: there is no raw SPI app (SECURITY_SCOPE).
 */
typedef struct stark_module_spi stark_module_spi_t;
stark_err_t stark_module_spi_attach(const stark_module_lease_t *l, uint32_t hz, uint8_t mode,
                                    stark_module_spi_t **out);
stark_err_t stark_module_spi_xfer(stark_module_spi_t *d, const void *tx, void *rx, size_t len);
void stark_module_spi_detach(stark_module_spi_t *d);
