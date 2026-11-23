/*
 * subghz_decode.h — generic OOK demodulators (TASKS.md STARK-0604, ADR-0022 §7): a raw
 * capture read as bits by pulse-width (PWM), pulse-distance (PPM) or Manchester timing, with
 * the timing that was measured. Pure: no I/O, no ESP-IDF, no allocation. Host-tested and
 * fuzzed (test_subghz_decode.c) against the independent reference in
 * scripts/gen_subghz_vectors.py.
 *
 * DISPLAY ONLY. There are no protocol names, no device database, no decoding of rolling or
 * encrypted codes, and nothing here turns bits back into symbols: the only transmit source
 * is a raw capture file (ADR-0022 §6). The rules are written out in docs/subghz-decode.md.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"
#include "stark_subghz_transport.h" /* subghz_sym_t */

#define SUBGHZ_DEC_MIN_BITS   8u   /* fewer bits is "unknown" */
#define SUBGHZ_DEC_MAX_BITS   128u /* more bits is "unknown" */
#define SUBGHZ_DEC_MAX_BURSTS 8u   /* distinct decoded bursts kept */
#define SUBGHZ_DEC_MAX_SYMS   2048u
#define SUBGHZ_DEC_HEX_SIZE   33u /* a burst's hex text, with the NUL */

typedef enum {
    SUBGHZ_DEC_UNKNOWN,
    SUBGHZ_DEC_PWM,
    SUBGHZ_DEC_PPM,
    SUBGHZ_DEC_MANCHESTER,
    SUBGHZ_DEC_KIND_COUNT
} subghz_dec_kind_t;

/* "unknown" / "pwm" / "ppm" / "manchester"; "?" for anything else. */
const char *subghz_dec_kind_name(subghz_dec_kind_t kind);

typedef struct {
    uint8_t kind;     /* a subghz_dec_kind_t, never SUBGHZ_DEC_UNKNOWN in a result */
    uint8_t bits;     /* SUBGHZ_DEC_MIN_BITS..SUBGHZ_DEC_MAX_BITS */
    uint16_t repeats; /* bursts that decoded to exactly these bits */
    /*
     * The measured clusters, µs (the mean of their members): PWM the short and long high,
     * PPM the short and long low, Manchester T and the 2T cluster.
     */
    uint32_t short_us;
    uint32_t long_us;
    uint8_t data[16]; /* the bits, first bit first (MSB), the last byte padded with zeros */
} subghz_dec_burst_t;

typedef struct {
    subghz_dec_burst_t burst[SUBGHZ_DEC_MAX_BURSTS]; /* distinct decodes, first seen first */
    uint8_t n;                                       /* used entries of burst[] */
    uint16_t decoded; /* bursts that decoded, repeats included: the sum of repeats + omitted */
    uint16_t unknown; /* bursts that did not decode */
    uint16_t omitted; /* decoded bursts that are not in burst[] (it was full of others) */
} subghz_dec_result_t;

/*
 * Splits the capture into bursts and decodes each one. The symbols are a capture's: n at
 * most 2048, alternating in sign and starting high, each |v| 1..1 000 000; STARK_ERR_INVALID_ARG
 * (and *out zeroed) for anything else, a NULL out, or a NULL sym with n > 0. n == 0 is a
 * valid empty capture. Never reads past n, never allocates, and the work is bounded by n.
 */
stark_err_t subghz_decode(const subghz_sym_t *sym, size_t n, subghz_dec_result_t *out);

/* The burst's bits as lower-case hex, two digits per byte (so 24 bits are six digits);
 * always NUL-terminated, truncated to fit size. */
void subghz_dec_hex(const subghz_dec_burst_t *b, char *out, size_t size);
