/*
 * subghz_raw.c — raw captures and the `rf` v1 file (subghz_raw.h). Pure.
 *
 * NOTE: the parser is a byte-fed state machine holding one line (160 bytes), so a
 * saved file is read in small chunks and never needs a whole-file buffer; it
 * is strict (it accepts exactly the grammar of docs/subghz-format.md) and never
 * indexes outside the symbol buffer it was given.
 */
#include "subghz_raw.h"
#include <stdio.h>
#include <string.h>

enum { ST_MAGIC, ST_FREQ, ST_MOD, ST_RATE, ST_SYMBOLS, ST_BODY, ST_DONE };

static uint32_t magnitude(subghz_sym_t v)
{
    return v < 0 ? (uint32_t)0 - (uint32_t)v : (uint32_t)v;
}

const char *subghz_mod_name(subghz_mod_t mod)
{
    if (mod == SUBGHZ_MOD_OOK) {
        return "ook";
    }
    return mod == SUBGHZ_MOD_2FSK ? "2fsk" : "?";
}

static bool freq_in_band(uint32_t hz)
{
    return hz >= SUBGHZ_BAND_MIN_HZ && hz <= SUBGHZ_BAND_MAX_HZ;
}

static bool rate_ok(uint32_t rate)
{
    return rate >= SUBGHZ_RAW_RATE_MIN && rate <= SUBGHZ_RAW_RATE_MAX;
}

uint32_t subghz_raw_duration_us(const subghz_raw_t *r)
{
    uint32_t total = 0;
    for (size_t i = 0; i < r->n; i++) {
        total += magnitude(r->sym[i]);
    }
    return total;
}

bool subghz_raw_valid(const subghz_raw_t *r)
{
    if (r == NULL || r->sym == NULL || r->n < 1 || r->n > SUBGHZ_RAW_MAX_SYMBOLS ||
        !freq_in_band(r->freq_hz) || (unsigned)r->mod >= (unsigned)SUBGHZ_MOD_COUNT ||
        !rate_ok(r->rate)) {
        return false;
    }
    uint64_t total = 0;
    for (size_t i = 0; i < r->n; i++) {
        subghz_sym_t v = r->sym[i];
        bool high = (i % 2u) == 0u;
        uint32_t m = magnitude(v);
        if ((high ? v <= 0 : v >= 0) || m > (uint32_t)SUBGHZ_RAW_SYM_MAX_US) {
            return false;
        }
        total += m;
    }
    return total <= SUBGHZ_RAW_MAX_US;
}

/* ---- accumulation ---------------------------------------------------------- */

void subghz_cap_init(subghz_cap_t *c, subghz_sym_t *buf, size_t cap)
{
    c->sym = buf;
    c->cap =
        (uint16_t)(cap > SUBGHZ_RAW_MAX_SYMBOLS ? SUBGHZ_RAW_MAX_SYMBOLS : (cap < 1 ? 1 : cap));
    c->n = 0;
    c->total_us = 0;
    c->full = false;
}

void subghz_cap_feed(subghz_cap_t *c, const subghz_sym_t *in, size_t n)
{
    for (size_t i = 0; i < n && !c->full; i++) {
        subghz_sym_t v = in[i];
        if (v == 0) {
            continue;
        }
        bool high = v > 0;
        if (c->n == 0 && !high) {
            continue; /* leading silence */
        }
        uint32_t add = magnitude(v);
        bool merge = c->n > 0 && (c->sym[c->n - 1] > 0) == high;
        uint32_t have = merge ? magnitude(c->sym[c->n - 1]) : 0u;
        if (add > (uint32_t)SUBGHZ_RAW_SYM_MAX_US - have) {
            add = (uint32_t)SUBGHZ_RAW_SYM_MAX_US - have; /* a symbol is at most 1 s */
        }
        if (add == 0) {
            continue;
        }
        uint32_t room = SUBGHZ_RAW_MAX_US - c->total_us;
        if (add >= room) {
            add = room; /* the capture spans 2 s: the last symbol is cut to fit */
            c->full = true;
        }
        c->total_us += add;
        subghz_sym_t out = (subghz_sym_t)(have + add);
        if (merge) {
            c->sym[c->n - 1] = high ? out : -out;
        } else {
            c->sym[c->n++] = high ? out : -out;
            if (c->n == c->cap) {
                c->full = true;
            }
        }
    }
}

/* ---- the file: writing ------------------------------------------------------ */

#define HEADER_UNITS 5u

static size_t render(const subghz_raw_t *r, size_t unit, char *b, size_t size)
{
    int n = 0;
    if (unit == 0) {
        n = snprintf(b, size, "stark-subghz 1\n");
    } else if (unit == 1) {
        n = snprintf(b, size, "freq %lu\n", (unsigned long)r->freq_hz);
    } else if (unit == 2) {
        n = snprintf(b, size, "mod %s\n", subghz_mod_name(r->mod));
    } else if (unit == 3) {
        n = snprintf(b, size, "rate %lu\n", (unsigned long)r->rate);
    } else if (unit == 4) {
        n = snprintf(b, size, "symbols %u\n", (unsigned)r->n);
    } else {
        size_t first = (unit - HEADER_UNITS) * SUBGHZ_RAW_LINE_SYMS;
        size_t last = first + SUBGHZ_RAW_LINE_SYMS < r->n ? first + SUBGHZ_RAW_LINE_SYMS : r->n;
        size_t used = 0;
        for (size_t i = first; i < last; i++) {
            used += (size_t)snprintf(b + used, size - used, i > first ? " %ld" : "%ld",
                                     (long)r->sym[i]);
        }
        b[used++] = '\n';
        return used;
    }
    return (size_t)n;
}

stark_err_t subghz_raw_format(const subghz_raw_t *r, size_t *cursor, char *out, size_t cap,
                              size_t *written)
{
    if (cursor == NULL || out == NULL || written == NULL || cap < SUBGHZ_RAW_CHUNK_MIN ||
        !subghz_raw_valid(r)) {
        return STARK_ERR_INVALID_ARG;
    }
    size_t units = HEADER_UNITS + ((size_t)r->n + SUBGHZ_RAW_LINE_SYMS - 1u) / SUBGHZ_RAW_LINE_SYMS;
    char line[160]; /* the longest line: 16 x "-1000000" and 15 spaces, then a newline */
    size_t used = 0;
    while (*cursor < units) {
        size_t n = render(r, *cursor, line, sizeof line);
        if (n > cap - used) {
            break;
        }
        memcpy(out + used, line, n);
        used += n;
        (*cursor)++;
    }
    *written = used;
    return STARK_OK;
}

/* ---- the file: reading ------------------------------------------------------ */

void subghz_raw_parser_init(subghz_raw_parser_t *p, subghz_sym_t *buf, size_t cap)
{
    memset(p, 0, sizeof *p);
    p->buf = buf;
    p->cap = buf == NULL ? 0 : cap;
}

static bool is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static bool next_token(const char *s, size_t len, size_t *pos, const char **tok, size_t *tlen)
{
    while (*pos < len && is_blank(s[*pos])) {
        (*pos)++;
    }
    if (*pos >= len) {
        return false;
    }
    *tok = s + *pos;
    while (*pos < len && !is_blank(s[*pos])) {
        (*pos)++;
    }
    *tlen = (size_t)(s + *pos - *tok);
    return true;
}

static bool token_is(const char *tok, size_t tlen, const char *word)
{
    return strlen(word) == tlen && memcmp(tok, word, tlen) == 0;
}

/* Canonical decimal: no sign, no leading zero, at most `max`. */
static bool parse_uint(const char *tok, size_t tlen, uint32_t max, uint32_t *out)
{
    if (tlen < 1 || tlen > 10 || (tok[0] == '0' && tlen > 1)) {
        return false;
    }
    uint64_t v = 0;
    for (size_t i = 0; i < tlen; i++) {
        if (tok[i] < '0' || tok[i] > '9') {
            return false;
        }
        v = v * 10u + (uint64_t)(tok[i] - '0');
    }
    if (v > max) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

/* A header line `key value` and nothing else; the value token. */
static bool key_value(const char *line, size_t len, const char *key, const char **val, size_t *vlen)
{
    size_t pos = 0;
    const char *tok;
    size_t tlen;
    const char *extra;
    size_t elen;
    return next_token(line, len, &pos, &tok, &tlen) && token_is(tok, tlen, key) &&
           next_token(line, len, &pos, val, vlen) && !next_token(line, len, &pos, &extra, &elen);
}

static bool header_line(subghz_raw_parser_t *p)
{
    const char *val;
    size_t vlen;
    uint32_t v;
    switch (p->stage) {
        case ST_MAGIC:
            return key_value(p->line, p->len, "stark-subghz", &val, &vlen) &&
                   token_is(val, vlen, "1");
        case ST_FREQ:
            if (!key_value(p->line, p->len, "freq", &val, &vlen) ||
                !parse_uint(val, vlen, UINT32_MAX, &v) || !freq_in_band(v)) {
                return false;
            }
            p->freq_hz = v;
            return true;
        case ST_MOD:
            if (!key_value(p->line, p->len, "mod", &val, &vlen)) {
                return false;
            }
            if (token_is(val, vlen, "ook")) {
                p->mod = SUBGHZ_MOD_OOK;
            } else if (token_is(val, vlen, "2fsk")) {
                p->mod = SUBGHZ_MOD_2FSK;
            } else {
                return false;
            }
            return true;
        case ST_RATE:
            if (!key_value(p->line, p->len, "rate", &val, &vlen) ||
                !parse_uint(val, vlen, UINT32_MAX, &v) || !rate_ok(v)) {
                return false;
            }
            p->rate = v;
            return true;
        default: /* ST_SYMBOLS */
            if (!key_value(p->line, p->len, "symbols", &val, &vlen) ||
                !parse_uint(val, vlen, SUBGHZ_RAW_MAX_SYMBOLS, &v) || v < 1 || v > p->cap) {
                return false;
            }
            p->declared = v;
            return true;
    }
}

/* One line of symbols: at most 16, alternating in sign from a high, within the 2 s. */
static bool symbol_line(subghz_raw_parser_t *p)
{
    size_t pos = 0;
    const char *tok;
    size_t tlen;
    unsigned count = 0;
    while (next_token(p->line, p->len, &pos, &tok, &tlen)) {
        uint32_t m;
        bool neg = tok[0] == '-';
        if (count == SUBGHZ_RAW_LINE_SYMS || p->got == p->declared ||
            !parse_uint(tok + (neg ? 1 : 0), tlen - (neg ? 1u : 0u), SUBGHZ_RAW_SYM_MAX_US, &m) ||
            m < 1 || neg != ((p->got % 2u) == 1u)) {
            return false;
        }
        p->total_us += m;
        if (p->total_us > SUBGHZ_RAW_MAX_US) {
            return false;
        }
        p->buf[p->got++] = neg ? -(subghz_sym_t)m : (subghz_sym_t)m;
        count++;
    }
    return true;
}

static bool line_ok(subghz_raw_parser_t *p)
{
    size_t pos = 0;
    const char *tok;
    size_t tlen;
    if (!next_token(p->line, p->len, &pos, &tok, &tlen)) {
        return true; /* blank, or a comment on its own */
    }
    if (p->stage == ST_DONE) {
        return false; /* nothing after the last symbol */
    }
    if (p->stage != ST_BODY) {
        if (!header_line(p)) {
            return false;
        }
        p->stage++;
        return true;
    }
    if (!symbol_line(p)) {
        return false;
    }
    if (p->got == p->declared) {
        p->stage = ST_DONE;
    }
    return true;
}

stark_err_t subghz_raw_parser_feed(subghz_raw_parser_t *p, const char *data, size_t len)
{
    if (p->failed || (data == NULL && len > 0) || len > SUBGHZ_RAW_FILE_MAX - p->bytes) {
        p->failed = true;
        return STARK_ERR_INVALID_ARG;
    }
    p->bytes += len;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        bool bad = false;
        if (c == '\0') {
            bad = true;
        } else if (c == '\n') {
            bad = !line_ok(p);
            p->len = 0;
            p->in_comment = false;
        } else if (p->in_comment) {
            continue;
        } else if (c == '#') {
            p->in_comment = true;
        } else {
            bad = !(c == '\t' || c == '\r' || (c >= 0x20 && c < 0x7F)) || p->len == sizeof p->line;
            if (!bad) {
                p->line[p->len++] = (char)c;
            }
        }
        if (bad) {
            p->failed = true;
            return STARK_ERR_INVALID_ARG;
        }
    }
    return STARK_OK;
}

stark_err_t subghz_raw_parser_finish(subghz_raw_parser_t *p, subghz_raw_t *out)
{
    *out = (subghz_raw_t){0};
    if (!p->failed && p->len > 0) {
        p->failed = !line_ok(p); /* a last line without its newline */
        p->len = 0;
    }
    if (p->failed || p->stage != ST_DONE) {
        p->failed = true;
        return STARK_ERR_INVALID_ARG;
    }
    out->freq_hz = p->freq_hz;
    out->mod = p->mod;
    out->rate = p->rate;
    out->sym = p->buf;
    out->n = (uint16_t)p->declared;
    return STARK_OK;
}

stark_err_t subghz_raw_parse(const char *text, size_t len, subghz_sym_t *buf, size_t cap,
                             subghz_raw_t *out)
{
    subghz_raw_parser_t p;
    subghz_raw_parser_init(&p, buf, cap);
    if (subghz_raw_parser_feed(&p, text, len) != STARK_OK) {
        *out = (subghz_raw_t){0};
        return STARK_ERR_INVALID_ARG;
    }
    return subghz_raw_parser_finish(&p, out);
}

uint32_t subghz_raw_hash(const subghz_sym_t *sym, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        uint32_t v = (uint32_t)sym[i];
        for (unsigned b = 0; b < 4; b++) {
            h ^= (v >> (8u * b)) & 0xFFu;
            h *= 16777619u;
        }
    }
    return h;
}

/* ---- histogram -------------------------------------------------------------- */

void subghz_hist(const subghz_sym_t *sym, size_t n, subghz_hist_t *out)
{
    memset(out, 0, sizeof *out);
    uint32_t done = 0; /* every duration up to here is in a bin */
    for (unsigned b = 0; b < SUBGHZ_RAW_BINS; b++) {
        uint32_t lo = UINT32_MAX;
        for (size_t i = 0; i < n; i++) {
            uint32_t m = magnitude(sym[i]);
            if (m > done && m < lo) {
                lo = m;
            }
        }
        if (lo == UINT32_MAX) {
            return;
        }
        uint32_t hi = lo + lo / 2u;
        uint32_t count = 0;
        uint64_t sum = 0;
        uint32_t top = lo;
        for (size_t i = 0; i < n; i++) {
            uint32_t m = magnitude(sym[i]);
            if (m >= lo && m <= hi) {
                count++;
                sum += m;
                top = m > top ? m : top;
            }
        }
        out->bin[b] =
            (subghz_bin_t){(uint16_t)count, (uint32_t)((sum + count / 2u) / count), lo, top};
        out->bins++;
        done = hi;
    }
    for (size_t i = 0; i < n; i++) {
        out->other = (uint16_t)(out->other + (magnitude(sym[i]) > done ? 1u : 0u));
    }
}

/* ---- names ------------------------------------------------------------------ */

uint32_t subghz_name_number(const char *name)
{
    if (name == NULL || strlen(name) != 10 || memcmp(name, "rf_", 3) != 0 ||
        strcmp(name + 7, ".rf") != 0) {
        return 0;
    }
    uint32_t v = 0;
    for (size_t i = 3; i < 7; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return 0;
        }
        v = v * 10u + (uint32_t)(name[i] - '0');
    }
    return v;
}

void subghz_name_scan(uint32_t *highest, const char *name)
{
    uint32_t v = subghz_name_number(name);
    if (v > *highest) {
        *highest = v;
    }
}

stark_err_t subghz_name_next(uint32_t highest, char *out, size_t len)
{
    if (out == NULL || len < 11) {
        return STARK_ERR_INVALID_ARG;
    }
    if (highest >= SUBGHZ_NAME_MAX - 1u) {
        return STARK_ERR_NO_SPACE;
    }
    (void)snprintf(out, len, "rf_%04lu.rf", (unsigned long)(highest + 1u));
    return STARK_OK;
}
