/*
 * pn532_frame.h — private to stark_nfc: PN532 frames (TASKS.md STARK-0402,
 * NXP UM0701-02 §6.2). Pure; host-tested against spec-derived vectors.
 *
 * A normal information frame is 00 00 FF LEN LCS TFI PD... DCS 00, where
 * LEN counts TFI and PD, LEN + LCS = 0 and TFI + PD + DCS = 0 (mod 256). TFI
 * is D4 from the host and D5 from the chip.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

#define PN532_DATA_MAX       254 /* LEN of a normal frame: TFI + PD */
#define PN532_FRAME_OVERHEAD 7   /* 00 00 FF LEN LCS ... DCS 00 */
#define PN532_TFI_HOST       0xD4u
#define PN532_TFI_CHIP       0xD5u

typedef enum {
    PN532_RX_ACK,
    PN532_RX_NACK,
    PN532_RX_DATA,     /* *data = TFI, PD...; *dlen = LEN */
    PN532_RX_APP_ERROR /* 00 00 FF 01 FF 7F 81 00: the chip rejected the command */
} pn532_rx_t;

extern const uint8_t PN532_ACK[6];
extern const uint8_t PN532_NACK[6];

/* A host frame for cmd with n parameter bytes; 0 when n > PN532_DATA_MAX - 2
 * or cap is too small. */
size_t pn532_frame_build(uint8_t cmd, const uint8_t *params, size_t n, uint8_t *out, size_t cap);

/*
 * Parses one frame starting at buf[0]. STARK_ERR_INVALID_ARG for a bad
 * preamble, start code or postamble, or a frame cut short;
 * STARK_ERR_IO for a length or data checksum that does not add up;
 * STARK_ERR_NOT_SUPPORTED for an extended frame (00 00 FF FF FF ...).
 */
stark_err_t pn532_frame_parse(const uint8_t *buf, size_t len, pn532_rx_t *kind,
                              const uint8_t **data, size_t *dlen);
