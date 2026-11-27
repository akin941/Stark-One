/*
 * subghz_decode.c — generic PWM, PPM and Manchester demodulation of a raw capture
 * (subghz_decode.h, docs/subghz-decode.md). Pure and allocation-free.
 *
 * The capture is cut into bursts at every low longer than four times its shortest cluster. A
 * burst keeps its symbols up to the last high (the low after it is the gap, or was cut by the
 * end of the capture), so it is H L H L … H. Each of the three demodulators then either
 * accepts the burst whole or refuses it; exactly one acceptance decodes it, anything else is
 * "unknown" (a burst two kinds both read is ambiguous and is not guessed).
 *
 * All arithmetic is integer, so the host reference (scripts/gen_subghz_vectors.py) agrees to
 * the microsecond: a mean is rounded to the nearest µs (halves up), "within ±25 %" of m is
 * 4·|d − m| <= m.
 */
#include "subghz_decode.h"
#include <string.h>
#include "subghz_raw.h" /* the duration histogram */

#define GAP_FACTOR   4u /* a low longer than this many shortest clusters ends a burst */
#define SPLIT_ROUNDS 4u /* two-means refinements; the split settles in one or two */
#define CAND_COUNT   3u

typedef struct {
    uint32_t s; /* mean of the short cluster */
    uint32_t l; /* mean of the long cluster */
    uint32_t t; /* durations up to t are short, longer ones long */
} clus_t;

const char *subghz_dec_kind_name(subghz_dec_kind_t kind)
{
    switch (kind) {
        case SUBGHZ_DEC_UNKNOWN:
            return "unknown";
        case SUBGHZ_DEC_PWM:
            return "pwm";
        case SUBGHZ_DEC_PPM:
            return "ppm";
        case SUBGHZ_DEC_MANCHESTER:
            return "manchester";
        case SUBGHZ_DEC_KIND_COUNT:
        default:
            return "?";
    }
}

void subghz_dec_hex(const subghz_dec_burst_t *b, char *out, size_t size)
{
    static const char digit[] = "0123456789abcdef";
    size_t used = 0;
    size_t bytes = ((size_t)b->bits + 7u) / 8u;
    for (size_t i = 0; i < bytes && used + 2u < size; i++) {
        out[used++] = digit[b->data[i] >> 4];
        out[used++] = digit[b->data[i] & 0x0Fu];
    }
    if (size > 0) {
        out[used] = '\0';
    }
}

/* ---- helpers -------------------------------------------------------------------------- */

static uint32_t dur(subghz_sym_t v)
{
    return (uint32_t)(v < 0 ? -v : v); /* the capture was checked: |v| <= 1 000 000 */
}

/* sum/count rounded to the nearest µs, halves up (count > 0). */
static uint32_t mean(uint64_t sum, uint32_t count)
{
    return (uint32_t)((2u * sum + count) / (2u * (uint64_t)count));
}

/* d is within ±25 % of m. */
static bool near_to(uint32_t d, uint32_t m)
{
    uint64_t diff = d > m ? d - m : m - d;
    return diff * 4u <= m;
}

/* Sets bit i of data (first bit = MSB of byte 0) when one. */
static void put_bit(uint8_t *data, size_t i, bool one)
{
    if (one) {
        data[i / 8u] = (uint8_t)(data[i / 8u] | (0x80u >> (i % 8u)));
    }
}

/*
 * Splits the durations of sym[start], sym[start + step], … (count of them) into a short and a
 * long cluster by two-means. False unless there are two clusters, the long mean at least 1.5
 * times the short: one spread of ±25 % splits at about 1.3.
 */
static bool cluster2(const subghz_sym_t *sym, size_t start, size_t step, size_t count, clus_t *c)
{
    uint32_t lo = UINT32_MAX;
    uint32_t hi = 0;
    for (size_t i = 0; i < count; i++) {
        uint32_t d = dur(sym[start + i * step]);
        lo = d < lo ? d : lo;
        hi = d > hi ? d : hi;
    }
    if (count < 2u || lo == hi) {
        return false;
    }
    /* lo <= t < hi at the start and after every refinement (each side's mean lies inside
     * [lo, hi]), so neither side is ever empty */
    uint32_t t = lo + (hi - lo) / 2u;
    for (unsigned pass = 0;; pass++) {
        uint64_t ss = 0;
        uint64_t ls = 0;
        uint32_t sn = 0;
        uint32_t ln = 0;
        for (size_t i = 0; i < count; i++) {
            uint32_t d = dur(sym[start + i * step]);
            if (d <= t) {
                ss += d;
                sn++;
            } else {
                ls += d;
                ln++;
            }
        }
        c->s = mean(ss, sn);
        c->l = mean(ls, ln);
        c->t = t;
        uint32_t next = (c->s + c->l) / 2u;
        if (next == t || pass == SPLIT_ROUNDS) {
            break;
        }
        t = next;
    }
    return 2u * (uint64_t)c->l >= 3u * (uint64_t)c->s;
}

/* ---- the three demodulators: each takes H L H … H (m odd) and fills b or refuses -------- */

static bool dec_ppm(const subghz_sym_t *sym, size_t m, subghz_dec_burst_t *b)
{
    size_t bits = m / 2u; /* one bit per low between two highs */
    if (bits < SUBGHZ_DEC_MIN_BITS || bits > SUBGHZ_DEC_MAX_BITS) {
        return false;
    }
    uint64_t sum = 0;
    for (size_t i = 0; i <= bits; i++) {
        sum += dur(sym[2u * i]);
    }
    uint32_t pulse = mean(sum, (uint32_t)bits + 1u);
    for (size_t i = 0; i <= bits; i++) {
        if (!near_to(dur(sym[2u * i]), pulse)) {
            return false; /* the highs are not all equal */
        }
    }
    clus_t c;
    if (!cluster2(sym, 1, 2, bits, &c)) {
        return false;
    }
    for (size_t i = 0; i < bits; i++) {
        uint32_t d = dur(sym[2u * i + 1u]);
        bool one = d > c.t;
        if (!near_to(d, one ? c.l : c.s)) {
            return false;
        }
        put_bit(b->data, i, one);
    }
    b->kind = SUBGHZ_DEC_PPM;
    b->bits = (uint8_t)bits;
    b->short_us = c.s;
    b->long_us = c.l;
    return true;
}

static bool dec_pwm(const subghz_sym_t *sym, size_t m, subghz_dec_burst_t *b)
{
    size_t bits = (m + 1u) / 2u; /* one bit per high */
    if (bits < SUBGHZ_DEC_MIN_BITS || bits > SUBGHZ_DEC_MAX_BITS) {
        return false;
    }
    clus_t c;
    if (!cluster2(sym, 0, 2, bits, &c)) {
        return false;
    }
    uint64_t sum = 0;
    for (size_t i = 0; i + 1u < bits; i++) {
        sum += (uint64_t)dur(sym[2u * i]) + dur(sym[2u * i + 1u]);
    }
    uint32_t period = mean(sum, (uint32_t)bits - 1u);
    for (size_t i = 0; i < bits; i++) {
        uint32_t h = dur(sym[2u * i]);
        bool one = h > c.t; /* the long high is a 1 */
        uint32_t cluster = one ? c.l : c.s;
        if (!near_to(h, cluster)) {
            return false;
        }
        /* the low is what the period leaves of the high's cluster (the last bit's low is the
         * gap, not part of the burst) */
        if (i + 1u < bits &&
            (period <= cluster || !near_to(dur(sym[2u * i + 1u]), period - cluster))) {
            return false;
        }
        put_bit(b->data, i, one);
    }
    b->kind = SUBGHZ_DEC_PWM;
    b->bits = (uint8_t)bits;
    b->short_us = c.s;
    b->long_us = c.l;
    return true;
}

/*
 * One reading of a Manchester burst: every symbol is one half-bit (T) or two (2T); a cell is
 * two halves that differ, high-low is a 1 and low-high a 0. `shifted` means the burst starts
 * on the second half of its first cell (that cell's low half was lost in the silence before
 * the burst, so the cell is a 0); an odd half left at the end is a high whose low half is the
 * gap, a 1. A burst with a 2T symbol reads in at most one of the two alignments.
 */
static bool man_read(const subghz_sym_t *sym, size_t m, uint32_t t, bool shifted,
                     subghz_dec_burst_t *b)
{
    size_t half = shifted ? 1u : 0u;
    size_t bits = 0;
    bool first = false; /* the level of the open cell's first half; the shifted start is low */
    for (size_t i = 0; i < m; i++) {
        bool high = (i % 2u) == 0u;
        for (unsigned h = dur(sym[i]) > t ? 2u : 1u; h > 0; h--, half++) {
            if (half % 2u == 0u) {
                first = high;
            } else if (first == high || bits == SUBGHZ_DEC_MAX_BITS) {
                return false; /* not a Manchester cell, or longer than a burst may be */
            } else {
                put_bit(b->data, bits++, first);
            }
        }
    }
    if (half % 2u == 1u) { /* the last half is a high: its low is the gap */
        if (bits == SUBGHZ_DEC_MAX_BITS) {
            return false;
        }
        put_bit(b->data, bits++, true);
    }
    b->bits = (uint8_t)bits;
    return bits >= SUBGHZ_DEC_MIN_BITS;
}

static bool dec_manchester(const subghz_sym_t *sym, size_t m, subghz_dec_burst_t *b)
{
    clus_t c;
    if (!cluster2(sym, 0, 1, m, &c) || 2u * (uint64_t)c.l > 5u * (uint64_t)c.s) {
        return false; /* two clusters, the long one between 1.5 and 2.5 times the short */
    }
    for (size_t i = 0; i < m; i++) {
        uint32_t d = dur(sym[i]);
        if (!near_to(d, d > c.t ? c.l : c.s)) {
            return false;
        }
    }
    if (!man_read(sym, m, c.t, false, b)) {
        memset(b->data, 0, sizeof b->data);
        if (!man_read(sym, m, c.t, true, b)) {
            return false;
        }
    }
    b->kind = SUBGHZ_DEC_MANCHESTER;
    b->short_us = c.s;
    b->long_us = c.l;
    return true;
}

/* ---- bursts ----------------------------------------------------------------------------- */

static void add_burst(subghz_dec_result_t *out, const subghz_dec_burst_t *b)
{
    out->decoded++;
    for (unsigned i = 0; i < out->n; i++) {
        subghz_dec_burst_t *have = &out->burst[i];
        if (have->kind == b->kind && have->bits == b->bits &&
            memcmp(have->data, b->data, sizeof b->data) == 0) {
            have->repeats++;
            return;
        }
    }
    if (out->n == SUBGHZ_DEC_MAX_BURSTS) {
        out->omitted++;
        return;
    }
    out->burst[out->n] = *b;
    out->burst[out->n].repeats = 1u;
    out->n++;
}

/* sym[0..end) is one burst: it starts high; drop a closing low and decode what is left. */
static void one_burst(const subghz_sym_t *sym, size_t end, subghz_dec_result_t *out)
{
    size_t m = end % 2u == 0u ? end - 1u : end;
    subghz_dec_burst_t cand[CAND_COUNT];
    memset(cand, 0, sizeof cand);
    bool ok[CAND_COUNT] = {dec_ppm(sym, m, &cand[0]), dec_pwm(sym, m, &cand[1]),
                           dec_manchester(sym, m, &cand[2])};
    unsigned found = 0;
    unsigned which = 0;
    for (unsigned k = 0; k < CAND_COUNT; k++) {
        if (ok[k]) {
            found++;
            which = k;
        }
    }
    if (found == 1u) {
        add_burst(out, &cand[which]);
    } else {
        out->unknown++;
    }
}

stark_err_t subghz_decode(const subghz_sym_t *sym, size_t n, subghz_dec_result_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof *out);
    if (n == 0) {
        return STARK_OK;
    }
    if (sym == NULL || n > SUBGHZ_DEC_MAX_SYMS) {
        return STARK_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < n; i++) {
        bool high = (i % 2u) == 0u;
        if (sym[i] == 0 || sym[i] > SUBGHZ_RAW_SYM_MAX_US || sym[i] < -SUBGHZ_RAW_SYM_MAX_US ||
            (sym[i] > 0) != high) {
            return STARK_ERR_INVALID_ARG;
        }
    }

    /* the shortest cluster: the first histogram group of at least two symbols */
    subghz_hist_t h;
    subghz_hist(sym, n, &h);
    uint32_t shortest = 0;
    for (unsigned b = 0; b < h.bins && shortest == 0; b++) {
        shortest = h.bin[b].count >= 2u ? h.bin[b].mean_us : 0u;
    }
    if (shortest == 0) {
        return STARK_OK; /* no two symbols alike: nothing to split on */
    }

    uint32_t gap = GAP_FACTOR * shortest;
    size_t start = 0;
    for (size_t i = 0; i < n; i++) {
        if (sym[i] < 0 && dur(sym[i]) > gap) {
            one_burst(sym + start, i - start, out); /* the gap itself is not part of it */
            start = i + 1u;
        }
    }
    if (start < n) {
        one_burst(sym + start, n - start, out);
    }
    return STARK_OK;
}
