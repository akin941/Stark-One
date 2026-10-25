/*
 * nfc_dump.h — tag dumps (TASKS.md STARK-0405, docs/nfc-format.md §2): a
 * tag's identification and the Type 2 pages it returned to an
 * unauthenticated read, as `stark-nfc 1` text. Pure; host-tested.
 *
 * For inspection only: nothing reads a dump back onto a tag (ADR-0020 §5).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "nfc_types.h"
#include "stark_err.h"

#define NFC_DUMP_LINE_MAX 32     /* the longest line, "page 230 XXXXXXXX\n", fits */
#define NFC_DUMP_NAME_MAX 10000u /* nfc_0001.nfc .. nfc_9999.nfc */

typedef struct {
    nfc_tag_t tag;
    uint16_t n_pages; /* pages 0 .. n_pages-1 in mem; 0 for identification-only tags */
    uint8_t mem[NFC_T2_MAX_PAGES * NFC_T2_PAGE_BYTES];
} nfc_dump_t;

/*
 * Streams the text: from *cursor on, writes as many whole lines as fit in
 * cap (each NUL-terminated output is a run of lines), advances *cursor and
 * returns the bytes written; 0 once everything is out. Start with
 * *cursor = 0. cap must hold at least NFC_DUMP_LINE_MAX bytes.
 */
size_t nfc_dump_format(const nfc_dump_t *d, size_t *cursor, char *out, size_t cap);

/* Strict and bounded: the header and fields in order, pages consecutive from
 * 0, hex upper case. STARK_ERR_INVALID_ARG for anything else. */
stark_err_t nfc_dump_parse(const char *text, size_t len, nfc_dump_t *out);

/* File names: captures/nfc_NNNN.nfc, numbered upwards (as ir_names.h). */
bool nfc_dump_name_scan(uint32_t *highest, const char *name);
stark_err_t nfc_dump_name_next(uint32_t highest, char *out, size_t len);
