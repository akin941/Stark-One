/*
 * subghz_raw.h — raw Sub-GHz captures (TASKS.md STARK-0603, ADR-0022): the
 * accumulation of timed symbols, the `rf` v1 text file format (docs/subghz-format.md),
 * the duration histogram and the file names. Pure: no I/O, no ESP-IDF, no
 * allocation; every buffer is the caller's. Host-tested and fuzzed.
 *
 * A symbol is a subghz_sym_t (stark_subghz_transport.h): int32 µs, > 0 the data
 * line high (carrier on), < 0 low, |v| 1..1 000 000. A capture alternates sign and
 * starts high, has 1..2048 symbols and spans at most 2 s in total.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"
#include "stark_subghz_transport.h" /* subghz_sym_t */
#include "subghz_rssi.h"            /* the band's edges */

#define SUBGHZ_RAW_MAX_SYMBOLS  2048u
#define SUBGHZ_RAW_SYM_MAX_US   1000000
#define SUBGHZ_RAW_MAX_US       2000000u /* the sum of |symbol| */
#define SUBGHZ_RAW_LINE_SYMS    16u      /* symbols per file line, at most */
#define SUBGHZ_RAW_RATE_MIN     600u
#define SUBGHZ_RAW_RATE_MAX     50000u
#define SUBGHZ_RAW_RATE_DEFAULT 4800u  /* what a capture is received at (STARK-0603) */
#define SUBGHZ_RAW_FILE_MAX     24576u /* the parser reads no more than this; a file is < 19 kB */
#define SUBGHZ_RAW_CHUNK_MIN    200u   /* the smallest chunk subghz_raw_format() works with */
#define SUBGHZ_RAW_BINS         8u

typedef enum { SUBGHZ_MOD_OOK, SUBGHZ_MOD_2FSK, SUBGHZ_MOD_COUNT } subghz_mod_t;

/* "ook" / "2fsk" (the file's spelling); "?" for anything else. */
const char *subghz_mod_name(subghz_mod_t mod);

typedef struct {
    uint32_t freq_hz;
    subghz_mod_t mod;
    uint32_t rate; /* baud, 600..50 000 */
    subghz_sym_t *sym;
    uint16_t n;
} subghz_raw_t;

/* The sum of |symbol|, µs (at most 2 s for a valid capture). */
uint32_t subghz_raw_duration_us(const subghz_raw_t *r);

/*
 * True when r could be saved and would read back identically: frequency inside
 * 433.05–434.79 MHz, known modulation, rate 600..50 000 Bd, 1..2048 symbols that
 * alternate in sign starting high, each |v| 1..1 000 000, their sum at most 2 s.
 */
bool subghz_raw_valid(const subghz_raw_t *r);

/* ---- accumulating a capture ---------------------------------------------- */

typedef struct {
    subghz_sym_t *sym;
    uint16_t n;
    uint16_t cap;
    uint32_t total_us; /* the sum of |symbol| kept so far */
    bool full;         /* 2048 symbols or 2 s reached: nothing more is kept */
} subghz_cap_t;

/* buf has room for cap symbols; cap is limited to 2048 (and at least 1). */
void subghz_cap_init(subghz_cap_t *c, subghz_sym_t *buf, size_t cap);

/*
 * Appends symbols as the data line produced them. Leading silence (low symbols
 * before the first high) is dropped, a symbol of the same sign as the last is merged
 * into it, a value of 0 is ignored and |v| is limited to 1 000 000. The capture
 * is full at 2048 symbols or when the kept symbols span 2 s (the last one is
 * cut to fit); once full everything is ignored.
 */
void subghz_cap_feed(subghz_cap_t *c, const subghz_sym_t *in, size_t n);

/* ---- the `rf` v1 file ------------------------------------------------------ */

/*
 * Streams the file text of r in chunks of at least SUBGHZ_RAW_CHUNK_MIN bytes
 * (not NUL-terminated): *cursor starts at 0 and the call is repeated until it
 * reports *written == 0 with STARK_OK. Each call writes whole lines.
 * STARK_ERR_INVALID_ARG for an r that is not valid (nothing is written), a NULL
 * argument or a chunk that is too small.
 */
stark_err_t subghz_raw_format(const subghz_raw_t *r, size_t *cursor, char *out, size_t cap,
                              size_t *written);

typedef struct {
    subghz_sym_t *buf;
    size_t cap;
    uint8_t stage;
    bool failed;
    uint32_t freq_hz;
    subghz_mod_t mod;
    uint32_t rate;
    uint32_t declared;
    uint32_t got;
    uint32_t total_us;
    size_t bytes;
    uint8_t len;
    bool in_comment;
    char line[160];
} subghz_raw_parser_t;

/* The symbols go into buf (room for cap, at least the declared count). */
void subghz_raw_parser_init(subghz_raw_parser_t *p, subghz_sym_t *buf, size_t cap);

/*
 * Takes the next bytes of the file, in pieces of any size (a line may be split
 * anywhere). The parser is strict: STARK_ERR_INVALID_ARG, and from then on for
 * every call, for anything the format does not define (docs/subghz-format.md),
 * for a file beyond SUBGHZ_RAW_FILE_MAX bytes and for a NUL byte. It never reads
 * past len and never writes past buf.
 */
stark_err_t subghz_raw_parser_feed(subghz_raw_parser_t *p, const char *data, size_t len);

/*
 * The end of the file. STARK_OK only when the header and exactly the declared
 * number of symbols have been read; out then describes them. Any other outcome
 * leaves *out zeroed: a file never loads partly.
 */
stark_err_t subghz_raw_parser_finish(subghz_raw_parser_t *p, subghz_raw_t *out);

/* The whole text at once (tests, small files): init, feed, finish. */
stark_err_t subghz_raw_parse(const char *text, size_t len, subghz_sym_t *buf, size_t cap,
                             subghz_raw_t *out);

/* FNV-1a over the symbols as little-endian int32: a fingerprint for logs and tests. */
uint32_t subghz_raw_hash(const subghz_sym_t *sym, size_t n);

/* ---- timing histogram ------------------------------------------------------- */

typedef struct {
    uint16_t count;
    uint32_t mean_us; /* rounded to the nearest µs */
    uint32_t lo_us;   /* the shortest member */
    uint32_t hi_us;   /* the longest member */
} subghz_bin_t;

typedef struct {
    uint8_t bins;                      /* used entries of bin[] */
    subghz_bin_t bin[SUBGHZ_RAW_BINS]; /* ascending */
    uint16_t other;                    /* symbols longer than the last bin (more than 8 groups) */
} subghz_hist_t;

/*
 * Groups the durations |v| of n symbols: the shortest duration not yet grouped,
 * lo, starts a bin holding every duration up to 1.5 × lo (the window is ±20 %
 * about 1.25 × lo), at most 8 bins, ascending; what is longer than the eighth
 * bin is counted in `other`. Order-independent.
 */
void subghz_hist(const subghz_sym_t *sym, size_t n, subghz_hist_t *out);

/* ---- names: captures/rf_NNNN.rf -------------------------------------------- */

#define SUBGHZ_NAME_MAX 10000u /* rf_0001.rf .. rf_9999.rf */

/* NNNN of an `rf_NNNN.rf` name (1..9999), 0 for any other name. */
uint32_t subghz_name_number(const char *name);
/* Raises *highest to the number of a capture name; other names are ignored. */
void subghz_name_scan(uint32_t *highest, const char *name);
/* "rf_<highest + 1>.rf"; STARK_ERR_NO_SPACE once rf_9999.rf exists, STARK_ERR_INVALID_ARG
 * for a buffer under 11 bytes. */
stark_err_t subghz_name_next(uint32_t highest, char *out, size_t len);
