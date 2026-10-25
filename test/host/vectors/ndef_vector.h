/*
 * ndef_vector.h — one NDEF test vector (scripts/gen_nfc_vectors.py).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "ndef.h"
#include "stark_err.h"

typedef struct {
    uint8_t tnf;
    const uint8_t *type;
    uint8_t type_len;
    const uint8_t *id;
    uint8_t id_len;
    const uint8_t *payload;
    uint32_t payload_len;
} ndef_vec_record_t;

typedef struct {
    const char *label;
    const uint8_t *msg;
    uint16_t len;
    stark_err_t expect; /* ndef_parse() result */
    uint8_t count;
    const ndef_vec_record_t *recs;
    bool canonical; /* ndef_encode() reproduces msg byte for byte */
} ndef_vector_t;

/* A single-record message and what the URI/Text helpers make of it. */
typedef struct {
    const char *label;
    const uint8_t *msg;
    uint16_t len;
    char kind;          /* 'U' or 'T' */
    stark_err_t expect; /* ndef_uri_get() / ndef_text_get() result */
    const char *value;  /* the URI or text */
    const char *lang;   /* Text only */
    const char *line;   /* ndef_describe() of the record */
} ndef_rtd_vector_t;
