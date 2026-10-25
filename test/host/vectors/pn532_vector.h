/*
 * pn532_vector.h — PN532 frame test vectors (scripts/gen_nfc_vectors.py).
 */
#pragma once

#include <stdint.h>
#include "pn532_frame.h"
#include "stark_err.h"

typedef struct {
    const char *label;
    uint8_t cmd;
    const uint8_t *params;
    uint16_t n;
    const uint8_t *frame; /* what the host must send */
    uint16_t len;
} pn532_build_vector_t;

typedef struct {
    const char *label;
    const uint8_t *frame; /* what the chip sent */
    uint16_t len;
    stark_err_t expect;
    pn532_rx_t kind;
    const uint8_t *data; /* TFI + PD for a data frame */
    uint16_t dlen;
} pn532_parse_vector_t;
