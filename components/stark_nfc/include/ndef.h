/*
 * ndef.h — NDEF messages (TASKS.md STARK-0401): the NFC Forum NDEF record
 * format, plus the URI ("U") and Text ("T") record types. Pure; host-tested
 * against spec-derived vectors (scripts/gen_nfc_vectors.py, ADR-0020 §4).
 *
 * Records are views into the caller's message buffer: nothing is copied and
 * nothing is allocated. Every length is checked against the buffer before it
 * is used. The subset is documented in docs/nfc-format.md.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "nfc_types.h"
#include "stark_err.h"

#define NDEF_MAX_RECORDS 8

typedef enum {
    NDEF_TNF_EMPTY = 0,
    NDEF_TNF_WELL_KNOWN,
    NDEF_TNF_MIME,
    NDEF_TNF_URI,
    NDEF_TNF_EXTERNAL,
    NDEF_TNF_UNKNOWN,
    NDEF_TNF_UNCHANGED
} ndef_tnf_t;

typedef struct {
    ndef_tnf_t tnf;
    const uint8_t *type;
    uint8_t type_len;
    const uint8_t *id;
    uint8_t id_len;
    const uint8_t *payload;
    uint32_t payload_len;
} ndef_record_t;

typedef struct {
    ndef_record_t rec[NDEF_MAX_RECORDS];
    uint8_t count;
} ndef_message_t;

/*
 * Parses a whole message. STARK_ERR_INVALID_ARG for anything malformed: MB
 * not on exactly the first record, no ME, bytes after ME, a length past the
 * buffer, a long payload over NFC_NDEF_MAX, TNF 7, or lengths a TNF forbids.
 * STARK_ERR_NOT_SUPPORTED for chunked records (CF) and TNF Unchanged.
 * STARK_ERR_NO_SPACE for more than NDEF_MAX_RECORDS records.
 */
stark_err_t ndef_parse(const uint8_t *buf, size_t len, ndef_message_t *out);

/*
 * Encodes n records (MB on the first, ME on the last, SR whenever the payload
 * is at most 255 bytes, IL only with an ID). Returns the bytes written, or 0
 * when a record is invalid or cap is too small. The inverse of ndef_parse()
 * for every record it accepts.
 */
size_t ndef_encode(const ndef_record_t *recs, size_t n, uint8_t *out, size_t cap);

/* URI record: the prefix code (0x00-0x23) expanded. STARK_ERR_INVALID_ARG for
 * another record type, a reserved code or a NUL byte; STARK_ERR_NO_SPACE when
 * the URI does not fit cap with its terminator. */
stark_err_t ndef_uri_get(const ndef_record_t *r, char *out, size_t cap);

/* Builds a URI record for uri in payload (the longest prefix code that
 * matches). out points into payload, which must outlive it. */
stark_err_t ndef_uri_make(const char *uri, uint8_t *payload, size_t cap, ndef_record_t *out);

/* Text record, UTF-8 only: STARK_ERR_NOT_SUPPORTED for UTF-16;
 * STARK_ERR_INVALID_ARG for a language code outside 1..63 bytes, the reserved
 * bit set or a NUL byte; STARK_ERR_NO_SPACE when either string does not fit. */
stark_err_t ndef_text_get(const ndef_record_t *r, char *lang, size_t lcap, char *text, size_t tcap);
stark_err_t ndef_text_make(const char *lang, const char *text, uint8_t *payload, size_t cap,
                           ndef_record_t *out);

/* One line for a screen or a log: "URI https://…", "Text en: …",
 * "MIME text/plain 12 B", "External a.b:c 3 B", "Well-known Sp 20 B",
 * "Empty", "Unknown 4 B". Control characters become '?'; a line that does
 * not fit ends with "..." (cap >= 4). */
void ndef_describe(const ndef_record_t *r, char *out, size_t cap);
