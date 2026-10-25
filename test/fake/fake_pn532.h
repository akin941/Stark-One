/*
 * fake_pn532.h — a test-only PN532 as seen over I2C (TASKS.md STARK-0402,
 * ADR-0020 §3). Pure C, linked by the host tests and by the emulator test
 * image's substitute stark_nfc_transport; never by the production firmware.
 *
 * It answers frames, not API calls: the status byte and ready bit, ACK and
 * NACK, checksummed information frames, the application-error frame, and the
 * commands the driver uses — GetFirmwareVersion, SAMConfiguration,
 * RFConfiguration (items 1 and 5), InListPassiveTarget (106 kbit/s type A),
 * InDataExchange and InRelease — with a simulated tag behind them: Type 2
 * READ (0x30), WRITE (0xA2) and GET_VERSION (0x60), or an identification-only
 * tag that refuses every exchange. Its frame code is written independently of
 * stark_nfc's. RF and analogue behaviour are not modelled.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

#define FAKE_MAX_PAGES  231 /* NTAG216 */
#define FAKE_MAX_WRITES 256 /* WRITE page addresses remembered */

typedef struct { /* a simulated tag, spec-derived */
    uint8_t uid[10];
    uint8_t uid_len;
    uint16_t atqa;
    uint8_t sak;
    bool type2;
    bool has_version;
    uint8_t version[8]; /* GET_VERSION reply */
    uint16_t pages;
    uint16_t ro_from; /* pages >= ro_from ignore WRITE (0: none) */
    uint8_t mem[FAKE_MAX_PAGES * 4];
} fake_tag_t;

typedef enum {
    FAKE_NONE = 0,
    FAKE_NO_DEVICE,   /* every transfer NACKed at the address */
    FAKE_BUS_ERROR,   /* every transfer fails on the bus */
    FAKE_NEVER_READY, /* a command is ACKed but its response never becomes ready */
    FAKE_SEND_NACK,   /* the chip sends NACK instead of ACK */
    FAKE_BAD_ACK,     /* a corrupt ACK */
    FAKE_SILENT,      /* the chip ignores the command (no ACK) */
    FAKE_BAD_LCS,     /* response length checksum wrong */
    FAKE_BAD_DCS,     /* response data checksum wrong */
    FAKE_TRUNCATED,   /* response cut short */
    FAKE_WRONG_CODE,  /* response code is not command + 1 */
    FAKE_APP_ERROR,   /* the application-error frame */
    FAKE_EXTENDED,    /* an extended frame */
    FAKE_OTHER_IC,    /* GetFirmwareVersion reports another IC */
    FAKE_FAULT_COUNT
} fake_fault_t;

typedef struct {
    uint32_t frames;         /* valid host frames */
    uint32_t bad_frames;     /* host frames the chip ignored */
    uint32_t host_nacks;     /* responses resent on request */
    uint32_t aborts;         /* commands aborted by a host ACK */
    uint32_t list_field_off; /* InListPassiveTarget with the field off */
    bool field;              /* RF field state */
    uint8_t passive_retries; /* RFConfiguration item 5 */
    uint8_t last_cmd;
    uint32_t writes; /* WRITE commands seen */
    uint16_t write_page[FAKE_MAX_WRITES];
} fake_pn532_stats_t;

void fake_pn532_reset(void); /* power-on state: no tag, no fault, field off */
/* f for the next `times` commands (-1: until another call; FAKE_NONE clears).
 * FAKE_NO_DEVICE and FAKE_BUS_ERROR count transfers instead of commands. */
void fake_pn532_fault(fake_fault_t f, int times);
void fake_pn532_tag(const fake_tag_t *t);         /* copied; NULL: the field is empty */
void fake_pn532_vanish_after(unsigned exchanges); /* the tag leaves after n more exchanges */
const fake_tag_t *fake_pn532_current_tag(void);   /* its memory, after writes; NULL: none */

/* Transport-shaped: what an I2C write / read to the chip at 0x24 does. */
stark_err_t fake_pn532_write(const uint8_t *buf, size_t len);
stark_err_t fake_pn532_read(uint8_t *buf, size_t len);

const fake_pn532_stats_t *fake_pn532_stats(void);
