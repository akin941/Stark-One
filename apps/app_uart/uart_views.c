/*
 * uart_views.c — the UART app's rows and texts (uartapp.h). Pure: no service call, no UI call, no
 * ESP-IDF header; host-tested to every line (test_uart_views.c).
 */
#include <stdio.h>
#include <string.h>
#include "uartapp.h"

static const char *const k_home_label[UARTHOME_ROWS] = {
    "Monitor", "Send", "Loopback test", "Auto-baud", "Log", "Baud", "Format",
};

static const uint8_t k_cr_lf[] = {0x0D, 0x0A};
static const uint8_t k_at[] = {'A', 'T', 0x0D, 0x0A};
static const uint8_t k_query[] = {'?', 0x0D, 0x0A};
static const uint8_t k_ctrl_c[] = {0x03};
static const uint8_t k_ctrl_d[] = {0x04};

static const struct {
    const uint8_t *bytes;
    size_t n;
    const char *label;
} k_canned[UARTAPP_CANNED] = {
    {k_cr_lf, sizeof k_cr_lf, "\\r\\n"},        {k_at, sizeof k_at, "AT\\r\\n"},
    {k_query, sizeof k_query, "?\\r\\n"},       {k_ctrl_c, sizeof k_ctrl_c, "Ctrl-C (03)"},
    {k_ctrl_d, sizeof k_ctrl_d, "Ctrl-D (04)"},
};

const char *uartview_home_label(size_t row)
{
    return row < UARTHOME_ROWS ? k_home_label[row] : "";
}

const char *uartview_home_value(size_t row, bool log_on, size_t baud, size_t fmt,
                                const char *loop_text, const char *autobaud_text, char *buf,
                                size_t n)
{
    switch ((uarthome_row_t)row) {
        case UARTHOME_LOOP:
            return loop_text != NULL && loop_text[0] != '\0' ? loop_text : NULL;
        case UARTHOME_AUTOBAUD:
            return autobaud_text != NULL && autobaud_text[0] != '\0' ? autobaud_text : NULL;
        case UARTHOME_LOG:
            return log_on ? "on" : "off";
        case UARTHOME_BAUD:
            (void)snprintf(buf, n, "%u", (unsigned)stark_lab_uart_baud(baud));
            return buf;
        case UARTHOME_FORMAT: {
            const char *name = stark_lab_uart_fmt_name(fmt);
            return name != NULL ? name : "";
        }
        case UARTHOME_MONITOR:
        case UARTHOME_SEND:
        case UARTHOME_ROWS:
        default:
            return NULL;
    }
}

bool uartview_step(size_t *index, size_t count, int dir)
{
    if (dir > 0 && *index + 1u < count) {
        (*index)++;
        return true;
    }
    if (dir < 0 && *index > 0u) {
        (*index)--;
        return true;
    }
    return false;
}

/* ---- the monitor ------------------------------------------------------------------------------
 */

void uartview_count_text(uint32_t v, char *buf, size_t n)
{
    if (v < 10000u) {
        (void)snprintf(buf, n, "%u", (unsigned)v);
    } else if (v < 1000000u) {
        (void)snprintf(buf, n, "%u.%uk", (unsigned)(v / 1000u), (unsigned)((v % 1000u) / 100u));
    } else {
        (void)snprintf(buf, n, "%u.%uM", (unsigned)(v / 1000000u),
                       (unsigned)((v % 1000000u) / 100000u));
    }
}

void uartview_settings_text(size_t baud, size_t fmt, char *buf, size_t n)
{
    const char *name = stark_lab_uart_fmt_name(fmt);
    (void)snprintf(buf, n, "%u %s", (unsigned)stark_lab_uart_baud(baud), name != NULL ? name : "");
}

void uartview_stats_text(const stark_lab_uart_stats_t *st, char *buf, size_t n)
{
    char rx[12];
    char err[12];
    char lost[12];
    uint64_t errors = (uint64_t)st->framing + st->parity + st->overrun;
    uartview_count_text(st->rx, rx, sizeof rx);
    uartview_count_text(errors > UINT32_MAX ? UINT32_MAX : (uint32_t)errors, err, sizeof err);
    uartview_count_text(st->lost, lost, sizeof lost);
    (void)snprintf(buf, n, "rx %s  err %s  lost %s", rx, err, lost);
}

size_t uartview_per_row(bool hex)
{
    return hex ? 16u : 48u;
}

size_t uartview_total_rows(bool hex, size_t count)
{
    size_t per = uartview_per_row(hex);
    return (count + per - 1u) / per;
}

size_t uartview_max_scroll(bool hex, size_t count, size_t visible)
{
    size_t total = uartview_total_rows(hex, count);
    return total > visible ? total - visible : 0u;
}

size_t uartview_window(bool hex, size_t count, size_t scroll, size_t visible, size_t *first)
{
    size_t total = uartview_total_rows(hex, count);
    size_t shown = total < visible ? total : visible;
    size_t max = total - shown;
    scroll = scroll < max ? scroll : max;
    *first = total - shown - scroll;
    return shown;
}

void uartview_row_span(bool hex, size_t count, size_t row, size_t *skip, size_t *len)
{
    size_t per = uartview_per_row(hex);
    size_t total = uartview_total_rows(hex, count);
    if (row >= total) {
        *skip = count;
        *len = 0;
        return;
    }
    size_t after = total - 1u - row; /* rows newer than this one */
    size_t end = count - after * per;
    size_t start = end > per ? end - per : 0u;
    *skip = start;
    *len = end - start;
}

void uartview_row_text(bool hex, const uint8_t *bytes, size_t len, char *buf, size_t n)
{
    if (hex) {
        (void)stark_lab_uart_hex_row(bytes, len, buf, n);
    } else {
        (void)stark_lab_uart_ascii_row(bytes, len, buf, n);
    }
}

size_t uartview_scroll(size_t scroll, int delta, size_t max)
{
    if (delta < 0) {
        size_t down = (size_t)(-(long)delta);
        return scroll > down ? scroll - down : 0u;
    }
    size_t up = (size_t)delta;
    return scroll + up < max ? scroll + up : max;
}

void uartview_status_text(uartlog_state_t log, const char *name, uint32_t bytes, size_t scroll,
                          char *buf, size_t n)
{
    char count[12];
    size_t used = 0;
    buf[0] = '\0';
    switch (log) {
        case UARTLOG_ON:
            uartview_count_text(bytes, count, sizeof count);
            used = (size_t)snprintf(buf, n, "Log %s %s", name != NULL ? name : "", count);
            break;
        case UARTLOG_FULL:
            used = (size_t)snprintf(buf, n, "Log full");
            break;
        case UARTLOG_FAILED:
            used = (size_t)snprintf(buf, n, "Log stopped");
            break;
        case UARTLOG_OFF:
        default:
            break;
    }
    if (scroll > 0u && used < n) {
        (void)snprintf(buf + used, n - used, "%sback %u", used > 0u ? "  " : "", (unsigned)scroll);
    }
}

const char *uartview_error_text(stark_err_t err)
{
    switch (err) {
        case STARK_ERR_BUSY:
            return "pins in use";
        case STARK_ERR_NOT_SUPPORTED:
            return "pins not offered";
        case STARK_ERR_NO_MEM:
            return "no memory";
        case STARK_ERR_STATE:
            return "not ready";
        case STARK_OK:
        case STARK_ERR_INVALID_ARG:
        case STARK_ERR_TIMEOUT:
        case STARK_ERR_NOT_FOUND:
        case STARK_ERR_IO:
        case STARK_ERR_NO_SPACE:
        default:
            return "failed";
    }
}

/* ---- the Send view -----------------------------------------------------------------------------
 */

void uartview_send_cfg_reset(uartapp_send_cfg_t *c)
{
    *c = (uartapp_send_cfg_t){.count = 1};
}

const uint8_t *uartview_canned(size_t idx, size_t *n)
{
    if (idx >= UARTAPP_CANNED) {
        *n = 0;
        return NULL;
    }
    *n = k_canned[idx].n;
    return k_canned[idx].bytes;
}

const char *uartview_canned_label(size_t idx)
{
    return idx < UARTAPP_CANNED ? k_canned[idx].label : "";
}

size_t uartview_send_rows(const uartapp_send_cfg_t *c, uartrow_t *out)
{
    size_t n = 0;
    for (uint8_t i = 0; i < UARTAPP_CANNED; i++) {
        out[n++] = (uartrow_t){UARTROW_CANNED, i};
    }
    out[n++] = (uartrow_t){UARTROW_FILE, 0};
    out[n++] = (uartrow_t){UARTROW_COUNT, 0};
    out[n++] = (uartrow_t){UARTROW_NIBBLE, 0};
    for (uint8_t i = 0; i < c->count && i < UARTAPP_SEND_MAX; i++) {
        out[n++] = (uartrow_t){UARTROW_BYTE, i};
    }
    out[n++] = (uartrow_t){UARTROW_SEND, 0};
    return n;
}

/* NOTE: constants in flash, so the menu's 41 labels cost no RAM (a label buffer per row did). */
static const char *const k_byte_label[UARTAPP_SEND_MAX] = {
    "Byte 0",  "Byte 1",  "Byte 2",  "Byte 3",  "Byte 4",  "Byte 5",  "Byte 6",  "Byte 7",
    "Byte 8",  "Byte 9",  "Byte 10", "Byte 11", "Byte 12", "Byte 13", "Byte 14", "Byte 15",
    "Byte 16", "Byte 17", "Byte 18", "Byte 19", "Byte 20", "Byte 21", "Byte 22", "Byte 23",
    "Byte 24", "Byte 25", "Byte 26", "Byte 27", "Byte 28", "Byte 29", "Byte 30", "Byte 31",
};

const char *uartview_send_label(const uartrow_t *row)
{
    switch (row->kind) {
        case UARTROW_CANNED:
            return uartview_canned_label(row->idx);
        case UARTROW_FILE:
            return "File " UARTAPP_SEND_FILE;
        case UARTROW_COUNT:
            return "Bytes";
        case UARTROW_NIBBLE:
            return "Nibble";
        case UARTROW_BYTE:
            return row->idx < UARTAPP_SEND_MAX ? k_byte_label[row->idx] : "Byte ?";
        case UARTROW_SEND:
        default:
            return "Send bytes";
    }
}

const char *uartview_send_value(const uartrow_t *row, const uartapp_send_cfg_t *c,
                                const char *status, char *buf, size_t n)
{
    if (status != NULL && status[0] != '\0') {
        return status;
    }
    switch (row->kind) {
        case UARTROW_COUNT:
            (void)snprintf(buf, n, "%u", (unsigned)c->count);
            return buf;
        case UARTROW_NIBBLE:
            return c->low_nibble ? "low" : "high";
        case UARTROW_BYTE:
            (void)snprintf(buf, n, "%02x", row->idx < UARTAPP_SEND_MAX ? c->data[row->idx] : 0u);
            return buf;
        case UARTROW_CANNED:
        case UARTROW_FILE:
        case UARTROW_SEND:
        default:
            return NULL;
    }
}

bool uartview_send_adjust(uartapp_send_cfg_t *c, const uartrow_t *row, int dir)
{
    switch (row->kind) {
        case UARTROW_COUNT: {
            size_t count = c->count;
            if (!uartview_step(&count, UARTAPP_SEND_MAX + 1u, dir) || count == 0u) {
                return false; /* one byte at least */
            }
            c->count = (uint8_t)count;
            return true;
        }
        case UARTROW_NIBBLE:
            c->low_nibble = !c->low_nibble;
            return true;
        case UARTROW_BYTE: {
            if (row->idx >= UARTAPP_SEND_MAX || dir == 0) {
                return false;
            }
            unsigned b = c->data[row->idx];
            unsigned step = dir > 0 ? 1u : 15u; /* +1 and -1 inside a nibble */
            if (c->low_nibble) {
                b = (b & 0xF0u) | ((b + step) & 0x0Fu);
            } else {
                b = ((b + (step << 4)) & 0xF0u) | (b & 0x0Fu);
            }
            c->data[row->idx] = (uint8_t)b;
            return true;
        }
        case UARTROW_CANNED:
        case UARTROW_FILE:
        case UARTROW_SEND:
        default:
            return false;
    }
}

void uartview_tx_message(const char *pin_name, char *buf, size_t n)
{
    (void)snprintf(buf, n,
                   "%s is driven by every send. Own hardware only. 3.3 V logic. Connected "
                   "hardware may be damaged.",
                   pin_name != NULL ? pin_name : "TX");
}
