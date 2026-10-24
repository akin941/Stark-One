/*
 * ir_raw.c — the `.ir` text format (ir_raw.h). Pure. The parser never reads
 * past `len`, never allocates and accepts only what the format defines.
 */
#include "ir_raw.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ir_codec.h"

#define LINE_MAX_CHARS 1700 /* "timings " + 256 x "65535 " */
#define CARRIER_MIN    20000u
#define CARRIER_MAX    60000u

size_t ir_raw_format(const ir_signal_t *s, char *out, size_t len)
{
    if (s == NULL || out == NULL || len == 0 || s->timings.n > IR_MAX_DURATIONS) {
        return 0;
    }
    size_t used = 0;
    int n =
        snprintf(out, len,
                 "stark-ir 1\nprotocol %s\naddress 0x%04X\ncommand 0x%04X\n%s%scarrier %u\ntimings",
                 ir_protocol_name(s->frame.protocol), s->frame.address, s->frame.command,
                 s->frame.toggle ? "toggle 1\n" : "", s->frame.repeat ? "repeat 1\n" : "",
                 (unsigned)s->carrier_hz);
    if (n < 0 || (size_t)n >= len) {
        return 0;
    }
    used = (size_t)n;
    for (uint16_t i = 0; i < s->timings.n; i++) {
        n = snprintf(out + used, len - used, " %u", s->timings.us[i]);
        if (n < 0 || (size_t)n >= len - used) {
            return 0;
        }
        used += (size_t)n;
    }
    if (used + 2 > len) {
        return 0;
    }
    out[used++] = '\n';
    out[used] = '\0';
    return used;
}

/* ---- parsing -------------------------------------------------------- */

typedef struct {
    const char *p;
    const char *end;
} span_t;

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static void skip_spaces(span_t *s)
{
    while (s->p < s->end && is_space(*s->p)) {
        s->p++;
    }
}

/* Next token (non-space run) of the line; false at the end of the line. */
static bool token(span_t *s, span_t *tok)
{
    skip_spaces(s);
    if (s->p >= s->end) {
        return false;
    }
    tok->p = s->p;
    while (s->p < s->end && !is_space(*s->p)) {
        s->p++;
    }
    tok->end = s->p;
    return true;
}

static bool equals(span_t tok, const char *word)
{
    size_t n = strlen(word);
    return (size_t)(tok.end - tok.p) == n && memcmp(tok.p, word, n) == 0;
}

/* Decimal or 0x hex, at most `max`. */
static bool number(span_t tok, uint32_t max, uint32_t *out)
{
    const char *p = tok.p;
    uint32_t base = 10;
    if (tok.end - p > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16; /* a bare "0x" stays decimal and fails on the 'x' */
        p += 2;
    }
    uint32_t v = 0;
    for (; p < tok.end; p++) {
        uint32_t digit;
        if (*p >= '0' && *p <= '9') {
            digit = (uint32_t)(*p - '0');
        } else if (base == 16 && *p >= 'a' && *p <= 'f') {
            digit = (uint32_t)(*p - 'a' + 10);
        } else if (base == 16 && *p >= 'A' && *p <= 'F') {
            digit = (uint32_t)(*p - 'A' + 10);
        } else {
            return false;
        }
        v = v * base + digit;
        if (v > max) {
            return false;
        }
    }
    *out = v;
    return true;
}

static uint32_t default_carrier(ir_protocol_t p)
{
    ir_frame_t f = {.protocol = p};
    ir_timings_t scratch;
    uint32_t carrier = 38000u;
    switch (p) {
        case IR_PROTO_NEC:
        case IR_PROTO_NECX:
        case IR_PROTO_RC5:
        case IR_PROTO_RC6:
        case IR_PROTO_SIRC12:
        case IR_PROTO_SIRC15:
        case IR_PROTO_SIRC20:
            (void)ir_encode(&f, &scratch, &carrier);
            break;
        case IR_PROTO_UNKNOWN:
        case IR_PROTO_COUNT:
            break;
    }
    return carrier;
}

stark_err_t ir_raw_parse(const char *text, size_t len, ir_signal_t *out)
{
    if (text == NULL || out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    *out = (ir_signal_t){0};
    const char *p = text;
    const char *end = text + len;
    bool header = false, have_timings = false, have_carrier = false;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *line_end = nl != NULL ? nl : end;
        if (line_end - p > LINE_MAX_CHARS) {
            return STARK_ERR_INVALID_ARG;
        }
        const char *hash = memchr(p, '#', (size_t)(line_end - p));
        span_t line = {p, hash != NULL ? hash : line_end};
        p = nl != NULL ? nl + 1 : end;
        span_t key;
        if (!token(&line, &key)) {
            continue; /* blank or comment-only */
        }
        if (!header) {
            span_t v;
            uint32_t version;
            if (!equals(key, "stark-ir") || !token(&line, &v) || !number(v, 0xFFFF, &version) ||
                version != 1) {
                return STARK_ERR_INVALID_ARG;
            }
            header = true;
        } else if (equals(key, "timings")) {
            span_t v;
            out->timings.n = 0;
            while (token(&line, &v)) {
                uint32_t d;
                if (out->timings.n == IR_MAX_DURATIONS || !number(v, UINT16_MAX, &d) || d == 0) {
                    return STARK_ERR_INVALID_ARG;
                }
                out->timings.us[out->timings.n++] = (uint16_t)d;
            }
            have_timings = out->timings.n > 0;
        } else if (equals(key, "carrier")) {
            span_t v;
            uint32_t hz;
            if (!token(&line, &v) || !number(v, CARRIER_MAX, &hz) || hz < CARRIER_MIN) {
                return STARK_ERR_INVALID_ARG;
            }
            out->carrier_hz = hz;
            have_carrier = true;
        }
        /* protocol / address / command / toggle / repeat: informational,
         * re-derived below; anything else: ignored */
    }
    if (!header || !have_timings) {
        return STARK_ERR_INVALID_ARG;
    }
    (void)ir_decode(&out->timings, &out->frame);
    if (!have_carrier) {
        out->carrier_hz = default_carrier(out->frame.protocol);
    }
    return STARK_OK;
}
