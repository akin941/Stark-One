/*
 * uartapp.h — private to the UART app (TASKS.md STARK-0705, ADR-0023 §7): the rows and texts of
 * the home list, the monitor and the Send view, and the card log. uart_views.c holds no service
 * call and no UI call (pure, host-tested to every line); uart_log.c talks to stark_storage only;
 * app_uart.c drives the service and the screens.
 *
 * The Send view's byte editor is rows, like the I2C app's (a menu cannot take UP and DOWN for a
 * value): Bytes (1 to 32), Nibble (which nibble LEFT and RIGHT change) and Byte 0 to Byte n-1; a
 * nibble wraps inside itself, so every byte is at most 15 presses from any other.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"
#include "stark_lab.h"

#define UARTAPP_SEND_MAX  32u /* bytes of one send: the service's limit */
#define UARTAPP_CANNED    5u
#define UARTAPP_ROWS_MAX  (UARTAPP_CANNED + 1u + 3u + UARTAPP_SEND_MAX) /* canned, file, editor */
#define UARTAPP_VISIBLE   18u /* monitor rows on the 320x240 screen (mono10, 10 px each) */
#define UARTAPP_SEND_FILE "uart_send.txt" /* in the apps directory of the card */

/* ---- home ------------------------------------------------------------------------------------ */
typedef enum {
    UARTHOME_MONITOR,
    UARTHOME_SEND,
    UARTHOME_LOOP,
    UARTHOME_AUTOBAUD,
    UARTHOME_LOG,
    UARTHOME_BAUD,
    UARTHOME_FORMAT,
    UARTHOME_ROWS
} uarthome_row_t;

const char *uartview_home_label(size_t row); /* a constant; "" past the end */

/* The value column of a home row: written into `buf` or a constant, NULL for none. */
const char *uartview_home_value(size_t row, bool log_on, size_t baud, size_t fmt,
                                const char *loop_text, const char *autobaud_text, char *buf,
                                size_t n);

/* LEFT (-1) / RIGHT (+1) on a list index: moves one place, clamped to 0..count-1 (the list ends
 * are where it stops); returns true when it moved. */
bool uartview_step(size_t *index, size_t count, int dir);

/* ---- the monitor ------------------------------------------------------------------------------
 */

/* "115200 8N1": the settings; "rx 1234  err 0  lost 0": the counters (errors are framing, parity
 * and overrun together). Counts over 9 999 are written 12.3k or 1.2M. */
void uartview_settings_text(size_t baud, size_t fmt, char *buf, size_t n);
void uartview_stats_text(const stark_lab_uart_stats_t *st, char *buf, size_t n);
void uartview_count_text(uint32_t v, char *buf, size_t n);

/* Bytes per monitor row: 16 as hex, 48 as ASCII. */
size_t uartview_per_row(bool hex);

/* How many rows `count` bytes make (the oldest may be partial: rows are aligned to the newest
 * byte), how far the view can scroll back with `visible` rows on screen, and which rows show when
 * it is `scroll` rows back: `*first` is the index of the top row, the return the number shown. */
size_t uartview_total_rows(bool hex, size_t count);
size_t uartview_max_scroll(bool hex, size_t count, size_t visible);
size_t uartview_window(bool hex, size_t count, size_t scroll, size_t visible, size_t *first);

/* The bytes of row `row` (0 is the oldest): `*skip` bytes after the oldest, `*len` of them. */
void uartview_row_span(bool hex, size_t count, size_t row, size_t *skip, size_t *len);

/* One row's text from its bytes: hex digits with spaces, or printable ASCII. */
void uartview_row_text(bool hex, const uint8_t *bytes, size_t len, char *buf, size_t n);

/* UP/DOWN/page: the scroll after a move of `delta` rows (positive: back in time), clamped to
 * 0..max. */
size_t uartview_scroll(size_t scroll, int delta, size_t max);

/* The status line under the counters: the log's state, `scroll n` when scrolled back, or "". */
typedef enum { UARTLOG_OFF, UARTLOG_ON, UARTLOG_FULL, UARTLOG_FAILED } uartlog_state_t;
void uartview_status_text(uartlog_state_t log, const char *name, uint32_t bytes, size_t scroll,
                          char *buf, size_t n);

/* A short text for why a session would not open or a call failed: "pins in use", "no memory",
 * "failed". */
const char *uartview_error_text(stark_err_t err);

/* ---- the Send view -----------------------------------------------------------------------------
 */
typedef struct {
    uint8_t count; /* 1..32 */
    bool low_nibble;
    uint8_t data[UARTAPP_SEND_MAX];
} uartapp_send_cfg_t;

typedef enum {
    UARTROW_CANNED, /* a canned send, idx 0..4 */
    UARTROW_FILE,   /* the file uart_send.txt */
    UARTROW_COUNT,  /* how many bytes the editor sends */
    UARTROW_NIBBLE, /* which nibble LEFT/RIGHT change */
    UARTROW_BYTE,   /* byte idx of the editor */
    UARTROW_SEND    /* action: send the editor's bytes */
} uartrow_kind_t;

typedef struct {
    uartrow_kind_t kind;
    uint8_t idx;
} uartrow_t;

void uartview_send_cfg_reset(uartapp_send_cfg_t *c); /* one byte, 0x00, the high nibble */

/* The canned sends: bytes and a label ("\r\n", "AT\r\n", "?\r\n", "Ctrl-C (03)", "Ctrl-D (04)"). */
const uint8_t *uartview_canned(size_t idx, size_t *n); /* NULL past the end */
const char *uartview_canned_label(size_t idx);

/* The rows of the view, top to bottom; returns how many (at most UARTAPP_ROWS_MAX). */
size_t uartview_send_rows(const uartapp_send_cfg_t *c, uartrow_t *out);

/* A row's label ("Byte 3": always a constant) and value (NULL for none; `status`, when it is not
 * empty, takes the place of the value). */
const char *uartview_send_label(const uartrow_t *row);
const char *uartview_send_value(const uartrow_t *row, const uartapp_send_cfg_t *c,
                                const char *status, char *buf, size_t n);

/* LEFT/RIGHT on a row: true when the configuration changed. Only Bytes (clamped), Nibble (toggles)
 * and a byte (its nibble wraps inside itself) adjust. */
bool uartview_send_adjust(uartapp_send_cfg_t *c, const uartrow_t *row, int dir);

/* The confirmation of the first drive of the TX pin: names the pin, says "own hardware only". */
void uartview_tx_message(const char *pin_name, char *buf, size_t n);

/* ---- the card log (uart_log.c) ------------------------------------------------------------------
 */

/*
 * Appends the received bytes to logs/uart_NNNN.log (the next free number) through a 2 kB buffer,
 * flushing to the card every 4 kB and every second a byte waits, at most 1 MiB, then stops and says
 * so (the monitor goes on). All of it runs on the app worker: the card calls block. The file is
 * closed (and so synced) on stop, so the card can be ejected after it.
 */
stark_err_t uartlog_start(void); /* STARK_ERR_STATE without a card, else what the card says */
void uartlog_write(const uint8_t *data, size_t n, uint32_t now_ms);
void uartlog_tick(uint32_t now_ms);
void uartlog_stop(
    void); /* flushes, closes, logs `uartapp: log closed <name> <n> bytes crc 0x...` */
uartlog_state_t uartlog_state(void);
const char *uartlog_name(void);
uint32_t uartlog_bytes(void); /* bytes accepted (buffered or written) */
uint32_t uartlog_crc(void);   /* CRC-32 of exactly those bytes */
