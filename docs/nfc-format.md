# NFC data formats

What STARK ONE's NFC code reads and writes (TASKS.md V0.4, ADR-0020). Everything here is
implemented by pure, host-tested C in `components/stark_nfc` and checked against
spec-derived vectors (`scripts/gen_nfc_vectors.py`).

## 1. NDEF (STARK-0401, `ndef.h`)

An NDEF message is a sequence of records (NFC Forum NDEF specification). Each record
is:

| Field | Size | Notes |
| --- | --- | --- |
| Header | 1 | MB `0x80` first record · ME `0x40` last record · CF `0x20` chunk · SR `0x10` short · IL `0x08` ID present · TNF bits 0–2 |
| Type length | 1 | |
| Payload length | 1 (SR) or 4 (big-endian) | a long payload is at most 872 bytes (`NFC_NDEF_MAX`) |
| ID length | 1 | only with IL |
| Type, ID, payload | as given | |

**Accepted:**
- up to 8 records (`NDEF_MAX_RECORDS`), with MB on exactly the first record, ME on the
  last, and nothing after ME;
- the TNFs Empty (all lengths 0), Well-known, MIME, Absolute URI, External, and Unknown
  (type length 0).

**Rejected:**
- `STARK_ERR_INVALID_ARG`: TNF 7 (reserved), any length past the buffer, a type on an
  Empty/Unknown record, or a missing type on the others;
- `STARK_ERR_NOT_SUPPORTED`: chunked records (CF) and TNF Unchanged, which are refused
  whole and never half-parsed;
- `STARK_ERR_NO_SPACE`: more than 8 records.

**Interpreted record types** (NFC Forum RTDs):
- **URI** (`U`): byte 0 is the prefix code (0x00–0x23 from the URI RTD table, e.g.
  0x04 = `https://`), and the rest is the URI. Codes above 0x23 and NUL bytes are
  invalid. When writing, the longest matching prefix is used.
- **Text** (`T`): byte 0 is the status (bit 7 UTF-16, bit 6 reserved, bits 0–5
  language-code length 1–63), followed by the language code and then the text. UTF-8
  only; UTF-16 is `STARK_ERR_NOT_SUPPORTED`.

**Shown, not interpreted:** every other record type, including MIME, External, Smart
Poster and Absolute URI. It is described as `MIME text/plain 12 B`,
`External a.b:c 3 B`, `Well-known Sp 20 B`, `Unknown 4 B` or `Empty`. Control
characters appear as `?`, and a line that does not fit ends with `...`.

When encoding, `ndef_encode` sets MB/ME, uses the short form whenever a payload is at
most 255 bytes, and sets IL only with an ID. That is the exact inverse of parsing for
canonical messages.

## 2. Tag dumps (STARK-0405, `nfc_dump.h`)

`Save dump` in the NFC app writes the present tag to `captures/nfc_NNNN.nfc`, using the
next free number and an atomic write. The format is plain text:

```
stark-nfc 1
uid 04303B46515C67
atqa 0044
sak 00
tech NTAG213
pages 45
page 0 04303B87
page 1 46515C67
...
page 44 00000000
```

* The first line is the format and version. The fields follow in exactly this order:
  `uid` (8, 14 or 20 hex digits), `atqa` (4), `sak` (2), `tech` (a name from
  `nfc_tech_name()`) and `pages` (decimal, 0–231).
* Then `pages` lines `page <n> <8 hex digits>`, numbered from 0 with no gaps. Hex is
  upper case, and fields are separated by single spaces.
* Lines starting with `#` are comments and may appear anywhere. The final newline is
  optional; anything after the last page is rejected.
* **What a dump holds** is only what the tag returned to unauthenticated reads.
  - A Type 2 tag gives every page of the product (NTAG213 45, NTAG215 135, NTAG216 231),
    or `4 + CC size × 2` for other Type 2 tags.
  - NTAG password and PACK pages read as zeros, as the data sheet specifies.
  - MIFARE Classic and ISO-DEP tags give their identification and `pages 0`: no sector
    is read and no key is used.
* **Inspection only.** The firmware has no way to write a dump back to a tag (ADR-0020
  §5). Files are read on a PC; `nfc_dump_parse()` is the strict, bounded reference
  parser and is host-tested with fuzzing.
