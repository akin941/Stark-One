/*
 * nfc_dump.c — tag dumps (nfc_dump.h). Pure.
 */
#include "nfc_dump.h"
#include <stdio.h>
#include <string.h>

#define FIELDS 6 /* header, uid, atqa, sak, tech, pages */

static const char k_hex[] = "0123456789ABCDEF";

static size_t hex_bytes(const uint8_t *b, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = k_hex[b[i] >> 4];
        out[2 * i + 1] = k_hex[b[i] & 0x0Fu];
    }
    out[2 * n] = '\0';
    return 2 * n;
}

/* Line i of the text into line (NUL-terminated); its length. */
static size_t line_at(const nfc_dump_t *d, size_t i, char *line)
{
    char hex[2 * NFC_UID_MAX + 1];
    switch (i) {
        case 0:
            return (size_t)snprintf(line, NFC_DUMP_LINE_MAX, "stark-nfc 1\n");
        case 1:
            (void)hex_bytes(d->tag.uid, d->tag.uid_len, hex);
            return (size_t)snprintf(line, NFC_DUMP_LINE_MAX, "uid %s\n", hex);
        case 2:
            return (size_t)snprintf(line, NFC_DUMP_LINE_MAX, "atqa %04X\n", (unsigned)d->tag.atqa);
        case 3:
            return (size_t)snprintf(line, NFC_DUMP_LINE_MAX, "sak %02X\n", (unsigned)d->tag.sak);
        case 4:
            return (size_t)snprintf(line, NFC_DUMP_LINE_MAX, "tech %s\n",
                                    nfc_tech_name(d->tag.tech));
        case 5:
            return (size_t)snprintf(line, NFC_DUMP_LINE_MAX, "pages %u\n", (unsigned)d->n_pages);
        default: {
            size_t page = i - FIELDS;
            (void)hex_bytes(d->mem + page * NFC_T2_PAGE_BYTES, NFC_T2_PAGE_BYTES, hex);
            return (size_t)snprintf(line, NFC_DUMP_LINE_MAX, "page %u %s\n", (unsigned)page, hex);
        }
    }
}

size_t nfc_dump_format(const nfc_dump_t *d, size_t *cursor, char *out, size_t cap)
{
    if (d == NULL || cursor == NULL || out == NULL || cap < NFC_DUMP_LINE_MAX ||
        d->tag.uid_len > NFC_UID_MAX || d->n_pages > NFC_T2_MAX_PAGES) {
        return 0;
    }
    size_t n = 0;
    out[0] = '\0';
    while (*cursor < FIELDS + (size_t)d->n_pages) {
        char line[NFC_DUMP_LINE_MAX];
        size_t len = line_at(d, *cursor, line);
        if (n + len + 1 > cap) {
            break;
        }
        memcpy(out + n, line, len + 1);
        n += len;
        (*cursor)++;
    }
    return n;
}

/* ---- parsing ------------------------------------------------------------- */

typedef struct {
    const char *text;
    size_t len;
    size_t pos;
} reader_t;

/* The next line that is not a comment; false at the end. */
static bool next_line(reader_t *r, const char **line, size_t *n)
{
    while (r->pos < r->len) {
        const char *start = r->text + r->pos;
        const char *nl = memchr(start, '\n', r->len - r->pos);
        size_t llen = nl != NULL ? (size_t)(nl - start) : r->len - r->pos;
        r->pos += llen + (nl != NULL ? 1u : 0u);
        if (llen > 0 && start[0] == '#') {
            continue;
        }
        *line = start;
        *n = llen;
        return true;
    }
    return false;
}

static int hex_digit(char c)
{
    const char *p = c != '\0' ? strchr(k_hex, c) : NULL;
    return p != NULL ? (int)(p - k_hex) : -1;
}

/* Exactly 2*n upper-case hex digits into n bytes. */
static bool parse_hex(const char *s, size_t slen, uint8_t *out, size_t n)
{
    if (slen != 2 * n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        int hi = hex_digit(s[2 * i]);
        int lo = hex_digit(s[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

/* A decimal of 1..4 digits, no leading zero (except "0"), <= max. */
static bool parse_dec(const char *s, size_t slen, unsigned max, unsigned *out)
{
    if (slen == 0 || slen > 4 || (slen > 1 && s[0] == '0')) {
        return false;
    }
    unsigned v = 0;
    for (size_t i = 0; i < slen; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        v = v * 10u + (unsigned)(s[i] - '0');
    }
    *out = v;
    return v <= max;
}

/* "key value": the value of a line that starts with key and one space. */
static bool field(const char *line, size_t n, const char *key, const char **value, size_t *vlen)
{
    size_t k = strlen(key);
    if (n <= k + 1 || memcmp(line, key, k) != 0 || line[k] != ' ') {
        return false;
    }
    *value = line + k + 1;
    *vlen = n - k - 1;
    return true;
}

static bool parse_tech(const char *v, size_t vlen, nfc_tech_t *out)
{
    for (int t = 0; t < NFC_TECH_COUNT; t++) {
        const char *name = nfc_tech_name((nfc_tech_t)t);
        if (strlen(name) == vlen && memcmp(name, v, vlen) == 0) {
            *out = (nfc_tech_t)t;
            return true;
        }
    }
    return false;
}

static bool parse_page(const char *line, size_t n, unsigned want, uint8_t *mem)
{
    const char *v;
    size_t vlen;
    if (!field(line, n, "page", &v, &vlen)) {
        return false;
    }
    const char *sp = memchr(v, ' ', vlen);
    unsigned index;
    if (sp == NULL || !parse_dec(v, (size_t)(sp - v), NFC_T2_MAX_PAGES - 1, &index) ||
        index != want) {
        return false;
    }
    return parse_hex(sp + 1, vlen - (size_t)(sp - v) - 1, mem + index * NFC_T2_PAGE_BYTES,
                     NFC_T2_PAGE_BYTES);
}

stark_err_t nfc_dump_parse(const char *text, size_t len, nfc_dump_t *out)
{
    if (text == NULL || out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof *out);
    reader_t r = {text, len, 0};
    const char *line;
    const char *v;
    size_t n;
    size_t vlen;
    unsigned num;
    uint8_t two[2];
    if (!next_line(&r, &line, &n) || n != 11 || memcmp(line, "stark-nfc 1", 11) != 0) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!next_line(&r, &line, &n) || !field(line, n, "uid", &v, &vlen) ||
        (vlen != 8 && vlen != 14 && vlen != 20) || !parse_hex(v, vlen, out->tag.uid, vlen / 2)) {
        return STARK_ERR_INVALID_ARG;
    }
    out->tag.uid_len = (uint8_t)(vlen / 2);
    if (!next_line(&r, &line, &n) || !field(line, n, "atqa", &v, &vlen) ||
        !parse_hex(v, vlen, two, 2)) {
        return STARK_ERR_INVALID_ARG;
    }
    out->tag.atqa = (uint16_t)((two[0] << 8) | two[1]);
    if (!next_line(&r, &line, &n) || !field(line, n, "sak", &v, &vlen) ||
        !parse_hex(v, vlen, &out->tag.sak, 1)) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!next_line(&r, &line, &n) || !field(line, n, "tech", &v, &vlen) ||
        !parse_tech(v, vlen, &out->tag.tech)) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!next_line(&r, &line, &n) || !field(line, n, "pages", &v, &vlen) ||
        !parse_dec(v, vlen, NFC_T2_MAX_PAGES, &num)) {
        return STARK_ERR_INVALID_ARG;
    }
    out->n_pages = (uint16_t)num;
    out->tag.pages = (uint16_t)num;
    for (unsigned i = 0; i < num; i++) {
        if (!next_line(&r, &line, &n) || !parse_page(line, n, i, out->mem)) {
            return STARK_ERR_INVALID_ARG;
        }
    }
    return next_line(&r, &line, &n) ? STARK_ERR_INVALID_ARG : STARK_OK; /* nothing after */
}

bool nfc_dump_name_scan(uint32_t *highest, const char *name)
{
    if (name == NULL || strlen(name) != 12 || strncmp(name, "nfc_", 4) != 0 ||
        strcmp(name + 8, ".nfc") != 0) {
        return false;
    }
    uint32_t n = 0;
    for (int i = 4; i < 8; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        n = n * 10u + (uint32_t)(name[i] - '0');
    }
    if (n > *highest) {
        *highest = n;
    }
    return true;
}

stark_err_t nfc_dump_name_next(uint32_t highest, char *out, size_t len)
{
    if (out == NULL || len < 13) {
        return STARK_ERR_INVALID_ARG;
    }
    if (highest + 1 >= NFC_DUMP_NAME_MAX) {
        return STARK_ERR_NO_SPACE;
    }
    snprintf(out, len, "nfc_%04u.nfc", (unsigned)(highest + 1));
    return STARK_OK;
}
