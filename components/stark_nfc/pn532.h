/*
 * pn532.h — private to stark_nfc: the PN532 protocol driver (TASKS.md
 * STARK-0402, NXP UM0701-02). Pure: it reaches the chip only through the
 * transport it is given (ADR-0020 §2) — the I2C port on the target, the fake
 * PN532 in tests — so every framing, retry and timeout path is host-tested.
 *
 * Over I2C every read starts with a status byte whose bit 0 says the chip has
 * a frame ready; the frame follows in the same read. A command is: write the
 * frame, poll until ready, read the ACK, poll until ready, read the response.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "nfc_types.h"
#include "pn532_frame.h"
#include "stark_err.h"

#define PN532_RETRIES          2u /* repeats after a NACK, a bad ACK or a missing ACK */
#define PN532_RESENDS          2u /* host NACKs asking for a corrupt response again */
#define PN532_ACK_TIMEOUT_MS   50u
#define PN532_POLL_MS          1u

/* Commands (UM0701-02 §7) */
#define PN532_CMD_GET_FIRMWARE 0x02u
#define PN532_CMD_SAM_CONFIG   0x14u
#define PN532_CMD_RF_CONFIG    0x32u
#define PN532_CMD_DATA_EXCH    0x40u
#define PN532_CMD_LIST_PASSIVE 0x4Au
#define PN532_CMD_RELEASE      0x52u

#define PN532_IC               0x32u /* GetFirmwareVersion's IC byte for a PN532 */

typedef struct {
    stark_err_t (*write)(void *ctx, const uint8_t *buf, size_t len);
    stark_err_t (*read)(void *ctx, uint8_t *buf, size_t len); /* status byte first */
    uint32_t (*now_ms)(void *ctx);
    void (*delay_ms)(void *ctx, uint32_t ms);
    void *ctx;
} pn532_io_t;

typedef struct {
    uint32_t retries;         /* commands repeated */
    uint32_t naks_sent;       /* responses asked for again */
    uint32_t checksum_errors; /* frames whose LCS or DCS did not add up */
    uint32_t timeouts;        /* the chip never became ready */
    uint32_t bus_errors;      /* the transport failed */
} pn532_stats_t;

typedef struct {
    pn532_io_t io;
    pn532_stats_t stats;
    uint8_t buf[1 + PN532_DATA_MAX + PN532_FRAME_OVERHEAD]; /* status + frame */
} pn532_t;

void pn532_bind(pn532_t *d, const pn532_io_t *io);

/*
 * One command: params out, the response's data (after TFI and the response
 * code) into resp. STARK_ERR_NOT_FOUND when nothing answers at the address,
 * STARK_ERR_TIMEOUT when the chip never becomes ready (after a response
 * timeout the command is aborted with an ACK), STARK_ERR_IO for NACKs,
 * corrupt frames past the retries, a wrong TFI or response code, an
 * application-error frame or a transport failure, STARK_ERR_NOT_SUPPORTED for
 * an extended frame. Only cap bytes of response data are read, so a longer
 * response arrives cut short and fails as a corrupt frame (STARK_ERR_IO).
 */
stark_err_t pn532_command(pn532_t *d, uint8_t cmd, const uint8_t *params, size_t n, uint8_t *resp,
                          size_t cap, size_t *got, uint32_t timeout_ms);

/* GetFirmwareVersion: IC (0x32 for a PN532), version, revision. */
stark_err_t pn532_firmware(pn532_t *d, uint8_t *ic, uint8_t *ver, uint8_t *rev);
/* SAMConfiguration: normal mode, no SAM. */
stark_err_t pn532_sam_normal(pn532_t *d);
/* RFConfiguration item 1: the RF field on or off. */
stark_err_t pn532_field(pn532_t *d, bool on);
/* RFConfiguration item 5: how often InListPassiveTarget retries activation. */
stark_err_t pn532_passive_retries(pn532_t *d, uint8_t n);

/*
 * Module detection (TASKS.md STARK-0402): GetFirmwareVersion, then
 * SAMConfiguration normal mode, one passive activation retry (so a poll with
 * no tag returns at once) and the field off. STARK_ERR_NOT_FOUND when nothing
 * answers at the address, STARK_ERR_NOT_SUPPORTED for another IC (*ic says
 * which), otherwise the first failing command's error.
 */
stark_err_t pn532_detect(pn532_t *d, uint8_t *ic, uint8_t *ver, uint8_t *rev);
