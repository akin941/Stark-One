/*
 * ndef.c — NDEF messages (ndef.h). Pure.
 */
#include "ndef.h"
#include <stdio.h>
#include <string.h>

#define HDR_MB        0x80u
#define HDR_ME        0x40u
#define HDR_CF        0x20u
#define HDR_SR        0x10u
#define HDR_IL        0x08u
#define HDR_TNF       0x07u

#define TEXT_UTF16    0x80u
#define TEXT_RESERVED 0x40u
#define TEXT_LANG     0x3Fu

/* NFC Forum URI RTD, table 3: the abbreviations for codes 0x01-0x23. */
static const char *const k_prefix[] = {
    "",
    "http://www.",
    "https://www.",
    "http://",
    "https://",
    "tel:",
    "mailto:",
    "ftp://anonymous:anonymous@",
    "ftp://ftp.",
    "ftps://",
    "sftp://",
    "smb://",
    "nfs://",
    "ftp://",
    "dav://",
    "news:",
    "telnet://",
    "imap:",
    "rtsp://",
    "urn:",
    "pop:",
    "sip:",
    "sips:",
    "tftp:",
    "btspp://",
    "btl2cap://",
    "btgoep://",
    "tcpobex://",
    "irdaobex://",
    "file://",
    "urn:epc:id:",
    "urn:epc:tag:",
    "urn:epc:pat:",
    "urn:epc:raw:",
    "urn:epc:",
    "urn:nfc:",
};
#define PREFIX_COUNT (sizeof k_prefix / sizeof k_prefix[0])

static const uint8_t k_type_uri[] = {'U'};
static const uint8_t k_type_text[] = {'T'};

/* The length rules each TNF imposes (NDEF §3.3); shared by parse and encode. */
static bool lengths_ok(uint8_t tnf, uint8_t type_len, uint8_t id_len, uint32_t payload_len)
{
    switch (tnf) {
        case NDEF_TNF_EMPTY:
            return type_len == 0 && id_len == 0 && payload_len == 0;
        case NDEF_TNF_UNKNOWN:
            return type_len == 0;
        default:
            return type_len > 0;
    }
}

stark_err_t ndef_parse(const uint8_t *buf, size_t len, ndef_message_t *out)
{
    if (buf == NULL || out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    out->count = 0;
    size_t pos = 0;
    for (;;) {
        if (len - pos < 2) { /* header and type length */
            return STARK_ERR_INVALID_ARG;
        }
        uint8_t hdr = buf[pos++];
        uint8_t tnf = hdr & HDR_TNF;
        if (((hdr & HDR_MB) != 0) != (out->count == 0)) {
            return STARK_ERR_INVALID_ARG;
        }
        if (tnf == 7u) {
            return STARK_ERR_INVALID_ARG;
        }
        if ((hdr & HDR_CF) != 0 || tnf == NDEF_TNF_UNCHANGED) {
            return STARK_ERR_NOT_SUPPORTED;
        }
        uint8_t type_len = buf[pos++];
        uint32_t payload_len = 0;
        size_t len_bytes = (hdr & HDR_SR) != 0 ? 1u : 4u;
        if (len - pos < len_bytes) {
            return STARK_ERR_INVALID_ARG;
        }
        for (size_t i = 0; i < len_bytes; i++) {
            payload_len = (payload_len << 8) | buf[pos++];
        }
        if (payload_len > NFC_NDEF_MAX) {
            return STARK_ERR_INVALID_ARG;
        }
        uint8_t id_len = 0;
        if ((hdr & HDR_IL) != 0) {
            if (pos == len) {
                return STARK_ERR_INVALID_ARG;
            }
            id_len = buf[pos++];
        }
        if (!lengths_ok(tnf, type_len, id_len, payload_len) ||
            len - pos < (size_t)type_len + id_len + payload_len) {
            return STARK_ERR_INVALID_ARG;
        }
        if (out->count == NDEF_MAX_RECORDS) {
            return STARK_ERR_NO_SPACE;
        }
        ndef_record_t *r = &out->rec[out->count++];
        r->tnf = (ndef_tnf_t)tnf;
        r->type = buf + pos;
        r->type_len = type_len;
        pos += type_len;
        r->id = buf + pos;
        r->id_len = id_len;
        pos += id_len;
        r->payload = buf + pos;
        r->payload_len = payload_len;
        pos += payload_len;
        if ((hdr & HDR_ME) != 0) {
            return pos == len ? STARK_OK : STARK_ERR_INVALID_ARG;
        }
    }
}

static void put(uint8_t *out, size_t *pos, const uint8_t *src, size_t n)
{
    if (n > 0) {
        memcpy(out + *pos, src, n);
        *pos += n;
    }
}

size_t ndef_encode(const ndef_record_t *recs, size_t n, uint8_t *out, size_t cap)
{
    if (recs == NULL || out == NULL || n == 0 || n > NDEF_MAX_RECORDS) {
        return 0;
    }
    size_t pos = 0;
    for (size_t i = 0; i < n; i++) {
        const ndef_record_t *r = &recs[i];
        if ((unsigned)r->tnf >= NDEF_TNF_UNCHANGED || r->payload_len > NFC_NDEF_MAX ||
            !lengths_ok((uint8_t)r->tnf, r->type_len, r->id_len, r->payload_len)) {
            return 0;
        }
        bool sr = r->payload_len <= 255u;
        size_t need = 2u + (sr ? 1u : 4u) + (r->id_len > 0 ? 1u : 0u) + r->type_len + r->id_len +
                      r->payload_len;
        if (cap - pos < need) {
            return 0;
        }
        uint8_t hdr = (uint8_t)r->tnf;
        hdr |= i == 0 ? HDR_MB : 0u;
        hdr |= i == n - 1 ? HDR_ME : 0u;
        hdr |= sr ? HDR_SR : 0u;
        hdr |= r->id_len > 0 ? HDR_IL : 0u;
        out[pos++] = hdr;
        out[pos++] = r->type_len;
        if (sr) {
            out[pos++] = (uint8_t)r->payload_len;
        } else {
            for (int shift = 24; shift >= 0; shift -= 8) {
                out[pos++] = (uint8_t)(r->payload_len >> shift);
            }
        }
        if (r->id_len > 0) {
            out[pos++] = r->id_len;
        }
        put(out, &pos, r->type, r->type_len);
        put(out, &pos, r->id, r->id_len);
        put(out, &pos, r->payload, r->payload_len);
    }
    return pos;
}

static bool is_type(const ndef_record_t *r, const uint8_t *type)
{
    return r != NULL && r->tnf == NDEF_TNF_WELL_KNOWN && r->type_len == 1 && r->type[0] == type[0];
}

/* Copies n bytes as a C string; a NUL inside is malformed. */
static stark_err_t copy_str(char *out, size_t cap, size_t *pos, const uint8_t *src, size_t n)
{
    if (memchr(src, 0, n) != NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (cap - *pos <= n) {
        return STARK_ERR_NO_SPACE;
    }
    if (n > 0) {
        memcpy(out + *pos, src, n);
    }
    *pos += n;
    out[*pos] = '\0';
    return STARK_OK;
}

stark_err_t ndef_uri_get(const ndef_record_t *r, char *out, size_t cap)
{
    if (!is_type(r, k_type_uri) || out == NULL || cap == 0 || r->payload_len == 0 ||
        r->payload[0] >= PREFIX_COUNT) {
        return STARK_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    size_t pos = 0;
    const char *prefix = k_prefix[r->payload[0]];
    STARK_CHECK_RET(copy_str(out, cap, &pos, (const uint8_t *)prefix, strlen(prefix)));
    return copy_str(out, cap, &pos, r->payload + 1, r->payload_len - 1);
}

stark_err_t ndef_uri_make(const char *uri, uint8_t *payload, size_t cap, ndef_record_t *out)
{
    if (uri == NULL || uri[0] == '\0' || payload == NULL || out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    size_t code = 0;
    size_t best = 0;
    for (size_t i = 1; i < PREFIX_COUNT; i++) {
        size_t n = strlen(k_prefix[i]);
        if (n > best && strncmp(uri, k_prefix[i], n) == 0) {
            code = i;
            best = n;
        }
    }
    size_t rest = strlen(uri) - best;
    if (rest + 1 > cap || rest + 1 > NFC_NDEF_MAX) {
        return STARK_ERR_NO_SPACE;
    }
    payload[0] = (uint8_t)code;
    memcpy(payload + 1, uri + best, rest);
    *out = (ndef_record_t){.tnf = NDEF_TNF_WELL_KNOWN,
                           .type = k_type_uri,
                           .type_len = 1,
                           .payload = payload,
                           .payload_len = (uint32_t)(rest + 1)};
    return STARK_OK;
}

stark_err_t ndef_text_get(const ndef_record_t *r, char *lang, size_t lcap, char *text, size_t tcap)
{
    if (!is_type(r, k_type_text) || lang == NULL || text == NULL || lcap == 0 || tcap == 0 ||
        r->payload_len == 0) {
        return STARK_ERR_INVALID_ARG;
    }
    uint8_t status = r->payload[0];
    if ((status & TEXT_UTF16) != 0) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    size_t lang_len = status & TEXT_LANG;
    if ((status & TEXT_RESERVED) != 0 || lang_len == 0 || lang_len > r->payload_len - 1) {
        return STARK_ERR_INVALID_ARG;
    }
    size_t pos = 0;
    STARK_CHECK_RET(copy_str(lang, lcap, &pos, r->payload + 1, lang_len));
    pos = 0;
    return copy_str(text, tcap, &pos, r->payload + 1 + lang_len, r->payload_len - 1 - lang_len);
}

stark_err_t ndef_text_make(const char *lang, const char *text, uint8_t *payload, size_t cap,
                           ndef_record_t *out)
{
    if (lang == NULL || text == NULL || payload == NULL || out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    size_t lang_len = strlen(lang);
    size_t text_len = strlen(text);
    if (lang_len == 0 || lang_len > TEXT_LANG) {
        return STARK_ERR_INVALID_ARG;
    }
    size_t n = 1 + lang_len + text_len;
    if (n > cap || n > NFC_NDEF_MAX) {
        return STARK_ERR_NO_SPACE;
    }
    payload[0] = (uint8_t)lang_len;
    memcpy(payload + 1, lang, lang_len);
    memcpy(payload + 1 + lang_len, text, text_len);
    *out = (ndef_record_t){.tnf = NDEF_TNF_WELL_KNOWN,
                           .type = k_type_text,
                           .type_len = 1,
                           .payload = payload,
                           .payload_len = (uint32_t)n};
    return STARK_OK;
}

/* ---- describe ---------------------------------------------------------- */

typedef struct {
    char *out;
    size_t cap;
    size_t pos;
    bool full;
} line_t;

static void line_bytes(line_t *l, const uint8_t *src, size_t n)
{
    for (size_t i = 0; i < n && !l->full; i++) {
        if (l->pos + 1 >= l->cap) {
            l->full = true;
            break;
        }
        uint8_t c = src[i];
        l->out[l->pos++] = (c < 0x20u || c == 0x7Fu) ? '?' : (char)c;
    }
    l->out[l->pos] = '\0';
}

static void line_str(line_t *l, const char *s)
{
    line_bytes(l, (const uint8_t *)s, strlen(s));
}

static void line_size(line_t *l, uint32_t n)
{
    char num[16];
    snprintf(num, sizeof num, " %u B", (unsigned)n);
    line_str(l, num);
}

/* Ends a line that did not fit with "...", backing off to a UTF-8 boundary. */
static void line_finish(line_t *l)
{
    if (!l->full || l->cap < 4) {
        return;
    }
    size_t end = l->cap - 4;
    while (end > 0 && ((uint8_t)l->out[end] & 0xC0u) == 0x80u) {
        end--;
    }
    memcpy(l->out + end, "...", 4);
}

void ndef_describe(const ndef_record_t *r, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    line_t l = {.out = out, .cap = cap};
    out[0] = '\0';
    if (r == NULL) {
        return;
    }
    switch (r->tnf) {
        case NDEF_TNF_EMPTY:
            line_str(&l, "Empty");
            break;
        case NDEF_TNF_WELL_KNOWN:
            if (is_type(r, k_type_uri)) {
                line_str(&l, "URI ");
                if (r->payload_len > 0 && r->payload[0] < PREFIX_COUNT) {
                    line_str(&l, k_prefix[r->payload[0]]);
                    line_bytes(&l, r->payload + 1, r->payload_len - 1);
                } else {
                    line_str(&l, "(invalid)");
                }
            } else if (is_type(r, k_type_text)) {
                uint8_t status = r->payload_len > 0 ? r->payload[0] : 0u;
                size_t lang_len = status & TEXT_LANG;
                if ((status & TEXT_UTF16) != 0) {
                    line_str(&l, "Text (UTF-16)");
                } else if (r->payload_len == 0 || (status & TEXT_RESERVED) != 0 || lang_len == 0 ||
                           lang_len > r->payload_len - 1) {
                    line_str(&l, "Text (invalid)");
                } else {
                    line_str(&l, "Text ");
                    line_bytes(&l, r->payload + 1, lang_len);
                    line_str(&l, ": ");
                    line_bytes(&l, r->payload + 1 + lang_len, r->payload_len - 1 - lang_len);
                }
            } else {
                line_str(&l, "Well-known ");
                line_bytes(&l, r->type, r->type_len);
                line_size(&l, r->payload_len);
            }
            break;
        case NDEF_TNF_MIME:
            line_str(&l, "MIME ");
            line_bytes(&l, r->type, r->type_len);
            line_size(&l, r->payload_len);
            break;
        case NDEF_TNF_URI:
            line_str(&l, "URI ");
            line_bytes(&l, r->type, r->type_len);
            break;
        case NDEF_TNF_EXTERNAL:
            line_str(&l, "External ");
            line_bytes(&l, r->type, r->type_len);
            line_size(&l, r->payload_len);
            break;
        case NDEF_TNF_UNKNOWN:
        case NDEF_TNF_UNCHANGED: /* never produced by ndef_parse() */
        default:
            line_str(&l, "Unknown");
            line_size(&l, r->payload_len);
            break;
    }
    line_finish(&l);
}
