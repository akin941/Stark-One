/*
 * lab_uart.c — the UART tool's data rules (lab_uart.h) and the pure text helpers of stark_lab.h
 * (hex and ASCII rows, the send file, log names, CRC-32, loopback text). Pure and host-tested to
 * every line against the independent reference in scripts/gen_lab_vectors.py.
 */
#include "lab_uart.h"
#include <stdio.h>
#include <string.h>

static const uint32_t k_bauds[LAB_UART_BAUDS] = {1200,  2400,   4800,   9600,   19200, 38400,
                                                 57600, 115200, 230400, 460800, 921600};

static const lab_uart_fmt_t k_fmts[LAB_UART_FORMATS] = {
    {"8N1", 8, 'N', 1}, {"8E1", 8, 'E', 1}, {"8O1", 8, 'O', 1},
    {"8N2", 8, 'N', 2}, {"7E1", 7, 'E', 1},
};

uint32_t stark_lab_uart_baud(size_t index)
{
    return index < LAB_UART_BAUDS ? k_bauds[index] : 0u;
}

size_t lab_uart_baud_index(uint32_t baud)
{
    for (size_t i = 0; i < LAB_UART_BAUDS; i++) {
        if (k_bauds[i] == baud) {
            return i;
        }
    }
    return SIZE_MAX;
}

const lab_uart_fmt_t *lab_uart_fmt(size_t index)
{
    return index < LAB_UART_FORMATS ? &k_fmts[index] : NULL;
}

const char *stark_lab_uart_fmt_name(size_t index)
{
    const lab_uart_fmt_t *f = lab_uart_fmt(index);
    return f != NULL ? f->name : NULL;
}

uint32_t lab_uart_bits_per_char(size_t fmt)
{
    const lab_uart_fmt_t *f = lab_uart_fmt(fmt);
    if (f == NULL) {
        return 0u;
    }
    return 1u + f->data_bits + (f->parity != 'N' ? 1u : 0u) + f->stop_bits;
}

uint32_t lab_uart_wire_ms(size_t n, uint32_t baud, size_t fmt)
{
    uint32_t bits = lab_uart_bits_per_char(fmt);
    if (baud == 0u || bits == 0u) {
        return 0u;
    }
    uint64_t total_bits = (uint64_t)n * bits;
    return (uint32_t)((total_bits * 1000u + baud - 1u) / baud);
}

uint32_t lab_uart_loop_window_ms(uint32_t baud, size_t fmt)
{
    return 2u * lab_uart_wire_ms(STARK_LAB_UART_LOOP_LEN, baud, fmt) + 20u;
}

/* ---- the byte ring ----------------------------------------------------------------------------
 */

void lab_uart_ring_init(lab_uart_ring_t *r, uint8_t *buf, size_t cap)
{
    *r = (lab_uart_ring_t){.buf = buf, .cap = cap};
}

void lab_uart_ring_clear(lab_uart_ring_t *r)
{
    r->head = 0;
    r->count = 0;
    r->lost = 0;
    r->total = 0;
}

void lab_uart_ring_push(lab_uart_ring_t *r, const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (r->count == r->cap) {
            r->head = (r->head + 1u) % r->cap; /* the oldest byte is overwritten, and counted */
            r->lost++;
        } else {
            r->count++;
        }
        r->buf[(r->head + r->count - 1u) % r->cap] = data[i];
        r->total++;
    }
}

size_t lab_uart_ring_peek(const lab_uart_ring_t *r, size_t skip, uint8_t *out, size_t max)
{
    if (skip >= r->count) {
        return 0;
    }
    size_t n = r->count - skip;
    n = n < max ? n : max;
    for (size_t i = 0; i < n; i++) {
        out[i] = r->buf[(r->head + skip + i) % r->cap];
    }
    return n;
}

/* ---- the loopback pattern ---------------------------------------------------------------------
 */

void lab_uart_loop_pattern(uint8_t out[STARK_LAB_UART_LOOP_LEN])
{
    for (size_t i = 0; i < STARK_LAB_UART_LOOP_LEN; i++) {
        out[i] = (uint8_t)i;
    }
}

void lab_uart_loop_check(const uint8_t *rx, size_t n_rx, size_t sent, uint32_t framing,
                         uint32_t parity, uint32_t overrun, stark_lab_loop_t *out)
{
    *out = (stark_lab_loop_t){.sent = (uint32_t)sent,
                              .received = (uint32_t)n_rx,
                              .first_mismatch = -1,
                              .framing = framing,
                              .parity = parity,
                              .overrun = overrun};
    uint32_t line_errors = framing + parity + overrun;
    if (n_rx == 0u) {
        out->status = STARK_LAB_LOOP_NO_DATA;
        out->errors = (uint32_t)sent + line_errors;
        out->first_mismatch = sent > 0u ? 0 : -1;
        return;
    }
    uint8_t want[STARK_LAB_UART_LOOP_LEN];
    lab_uart_loop_pattern(want);
    uint32_t bad = 0;
    size_t span = n_rx > sent ? n_rx : sent; /* a missing or an extra byte is an error as well */
    for (size_t i = 0; i < span; i++) {
        bool differs = i >= n_rx || i >= sent || i >= STARK_LAB_UART_LOOP_LEN || rx[i] != want[i];
        if (differs) {
            bad++;
            if (out->first_mismatch < 0) {
                out->first_mismatch = (int32_t)i;
            }
        }
    }
    out->errors = bad + line_errors;
    if (bad > 0u) {
        out->status = STARK_LAB_LOOP_MISMATCH;
    } else if (line_errors > 0u) {
        out->status = STARK_LAB_LOOP_LINE_ERRORS;
    } else {
        out->status = STARK_LAB_LOOP_OK;
    }
}

void stark_lab_loop_text(const stark_lab_loop_t *r, char *buf, size_t n)
{
    switch (r->status) {
        case STARK_LAB_LOOP_OK:
            (void)snprintf(buf, n, "%u/%u ok", (unsigned)r->received, (unsigned)r->sent);
            break;
        case STARK_LAB_LOOP_MISMATCH:
            (void)snprintf(buf, n, "%u/%u differs at %d", (unsigned)r->received, (unsigned)r->sent,
                           (int)r->first_mismatch);
            break;
        case STARK_LAB_LOOP_LINE_ERRORS:
            (void)snprintf(buf, n, "%u/%u line errors %u", (unsigned)r->received, (unsigned)r->sent,
                           (unsigned)r->errors);
            break;
        case STARK_LAB_LOOP_NO_DATA:
        default:
            (void)snprintf(buf, n, "No data back");
            break;
    }
}

void stark_lab_loop_log_text(const stark_lab_loop_t *r, char *buf, size_t n)
{
    unsigned recv = (unsigned)r->received;
    unsigned sent = (unsigned)r->sent;
    switch (r->status) {
        case STARK_LAB_LOOP_OK:
            (void)snprintf(buf, n, "%u/%u ok", recv, sent);
            break;
        case STARK_LAB_LOOP_MISMATCH:
            (void)snprintf(buf, n, "%u/%u mismatch first %d", recv, sent, (int)r->first_mismatch);
            break;
        case STARK_LAB_LOOP_LINE_ERRORS:
            (void)snprintf(buf, n, "%u/%u errors %u", recv, sent, (unsigned)r->errors);
            break;
        case STARK_LAB_LOOP_NO_DATA:
        default:
            (void)snprintf(buf, n, "%u/%u no data", recv, sent);
            break;
    }
}

/* ---- text -------------------------------------------------------------------------------------
 */

size_t stark_lab_uart_hex_row(const uint8_t *b, size_t n, char *buf, size_t sz)
{
    static const char k_hex[] = "0123456789abcdef";
    size_t used = 0;
    if (sz == 0u) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        size_t need = (i > 0u ? 1u : 0u) + 2u;
        if (used + need + 1u > sz) {
            break;
        }
        if (i > 0u) {
            buf[used++] = ' ';
        }
        buf[used++] = k_hex[b[i] >> 4];
        buf[used++] = k_hex[b[i] & 0xFu];
    }
    buf[used] = '\0';
    return used;
}

size_t stark_lab_uart_ascii_row(const uint8_t *b, size_t n, char *buf, size_t sz)
{
    size_t used = 0;
    if (sz == 0u) {
        return 0;
    }
    for (size_t i = 0; i < n && used + 1u < sz; i++) {
        buf[used++] = (b[i] >= 0x20u && b[i] <= 0x7Eu) ? (char)b[i] : '.';
    }
    buf[used] = '\0';
    return used;
}

/* ---- the send file ----------------------------------------------------------------------------
 */

static bool is_space(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int hex_value(uint8_t c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* After `hex:`: pairs of digits, white space between them (and before the first, after the last).
 */
static stark_lab_file_t parse_hex(const uint8_t *t, size_t n, uint8_t *out, size_t *count)
{
    size_t produced = 0;
    size_t i = 0;
    while (i < n) {
        if (is_space(t[i])) {
            i++;
            continue;
        }
        int hi = hex_value(t[i]);
        int lo = i + 1u < n ? hex_value(t[i + 1u]) : -1;
        if (hi < 0 || lo < 0) {
            return STARK_LAB_FILE_BAD;
        }
        if (produced == STARK_LAB_UART_SEND_MAX) {
            return STARK_LAB_FILE_TOO_BIG;
        }
        out[produced++] = (uint8_t)((hi << 4) | lo);
        i += 2u;
    }
    *count = produced;
    return produced == 0u ? STARK_LAB_FILE_EMPTY : STARK_LAB_FILE_OK;
}

stark_lab_file_t stark_lab_uart_parse_send(const uint8_t *text, size_t n, uint8_t *out,
                                           size_t *count)
{
    *count = 0;
    if (n > STARK_LAB_UART_FILE_MAX) {
        return STARK_LAB_FILE_TOO_BIG;
    }
    if (n >= 4u && memcmp(text, "hex:", 4) == 0) {
        return parse_hex(text + 4, n - 4u, out, count);
    }
    if (n >= 2u && text[n - 2u] == '\r' && text[n - 1u] == '\n') {
        n -= 2u;
    } else if (n >= 1u && text[n - 1u] == '\n') {
        n -= 1u;
    }
    for (size_t i = 0; i < n; i++) {
        if (text[i] < 0x20u || text[i] > 0x7Eu) {
            return STARK_LAB_FILE_BAD;
        }
    }
    if (n == 0u) {
        return STARK_LAB_FILE_EMPTY;
    }
    if (n > STARK_LAB_UART_SEND_MAX) {
        return STARK_LAB_FILE_TOO_BIG;
    }
    memcpy(out, text, n);
    *count = n;
    return STARK_LAB_FILE_OK;
}

const char *stark_lab_uart_file_text(stark_lab_file_t why)
{
    switch (why) {
        case STARK_LAB_FILE_OK:
            return "ok";
        case STARK_LAB_FILE_EMPTY:
            return "empty";
        case STARK_LAB_FILE_TOO_BIG:
            return "too long";
        case STARK_LAB_FILE_BAD:
        default:
            return "bad bytes";
    }
}

/* ---- the log file -----------------------------------------------------------------------------
 */

bool stark_lab_uart_log_name_parse(const char *name, uint32_t *number)
{
    if (name == NULL || strlen(name) != 13u || memcmp(name, "uart_", 5) != 0 ||
        memcmp(name + 9, ".log", 4) != 0) {
        return false;
    }
    uint32_t v = 0;
    for (size_t i = 5; i < 9u; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        v = v * 10u + (uint32_t)(name[i] - '0');
    }
    if (number != NULL) {
        *number = v;
    }
    return true;
}

stark_err_t stark_lab_uart_log_name_next(uint32_t highest, char *buf, size_t n)
{
    if (buf == NULL || n < 14u) {
        return STARK_ERR_INVALID_ARG;
    }
    if (highest >= 9999u) {
        return STARK_ERR_NO_SPACE;
    }
    uint32_t v = highest + 1u;
    memcpy(buf, "uart_", 5);
    for (size_t i = 0; i < 4u; i++) {
        buf[8u - i] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    memcpy(buf + 9, ".log", 5); /* with the terminating NUL */
    return STARK_OK;
}

uint32_t stark_lab_uart_crc32(uint32_t crc, const uint8_t *data, size_t n)
{
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}
