/*
 * lab_uart_svc.c — the UART tool's session (lab_uart_svc.h). Pure and host-tested to every line.
 */
#include "lab_uart_svc.h"
#include <stdlib.h>
#include <string.h>

#define SEND_SLACK_MS 50u /* on top of a write's wire time */
#define DRAIN_CHUNK   64u

static void dlock(lab_uart_ses_t *s)
{
    if (s->io.data_lock != NULL) {
        s->io.data_lock(s->io.ctx);
    }
}

static void dunlock(lab_uart_ses_t *s)
{
    if (s->io.data_unlock != NULL) {
        s->io.data_unlock(s->io.ctx);
    }
}

static void say(lab_uart_ses_t *s, lab_uart_log_t what, const void *p, uint32_t a, uint32_t b)
{
    s->io.log(s->io.ctx, what, p, a, b);
}

void lab_uartsvc_init(lab_uart_ses_t *s, const lab_uart_io_t *io, const stark_board_pins_t *pins)
{
    *s = (lab_uart_ses_t){.io = *io, .tx_pin = pins->lab_uart_tx, .rx_pin = pins->lab_uart_rx};
}

/* Gives everything back: driver, ring, both pins. */
void lab_uartsvc_close(lab_uart_ses_t *s)
{
    if (!s->open) {
        return;
    }
    s->io.close(s->io.ctx);
    dlock(s);
    free(s->ring_buf);
    s->ring_buf = NULL;
    lab_uart_ring_init(&s->ring, NULL, 0);
    s->open = false;
    s->tx = false;
    dunlock(s);
    s->io.pin_release(s->io.ctx, s->rx_pin);
    s->io.pin_release(s->io.ctx, s->tx_pin);
    say(s, LAB_UART_LOG_CLOSED, NULL, 0, 0);
}

/* The session ended because the transport failed: close it and say why. */
static stark_err_t fail(lab_uart_ses_t *s, stark_err_t err)
{
    say(s, LAB_UART_LOG_FAILED, NULL, (uint32_t)err, 0);
    lab_uartsvc_close(s);
    return err;
}

static void zero_counters(lab_uart_ses_t *s)
{
    s->framing = 0;
    s->parity = 0;
    s->overrun = 0;
    s->pending_data = false;
    s->pending_err = false;
}

stark_err_t lab_uartsvc_open(lab_uart_ses_t *s, size_t baud_index, size_t fmt_index)
{
    if (baud_index >= LAB_UART_BAUDS || fmt_index >= LAB_UART_FORMATS) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s->open) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = s->io.pin_acquire(s->io.ctx, s->tx_pin);
    if (err != STARK_OK) {
        return err;
    }
    err = s->io.pin_acquire(s->io.ctx, s->rx_pin);
    if (err != STARK_OK) {
        s->io.pin_release(s->io.ctx, s->tx_pin);
        return err;
    }
    uint8_t *ring = malloc(STARK_LAB_UART_RING);
    err = ring == NULL ? STARK_ERR_NO_MEM
                       : s->io.open(s->io.ctx, stark_lab_uart_baud(baud_index), (uint8_t)fmt_index,
                                    LAB_UARTSVC_RXBUF, false);
    if (err != STARK_OK) {
        free(ring);
        s->io.pin_release(s->io.ctx, s->rx_pin);
        s->io.pin_release(s->io.ctx, s->tx_pin);
        return err;
    }
    s->ring_buf = ring;
    lab_uart_ring_init(&s->ring, ring, STARK_LAB_UART_RING);
    zero_counters(s);
    s->note_valid = false;
    s->open = true;
    s->tx = false;
    s->baud_i = baud_index;
    s->fmt_i = fmt_index;
    say(s, LAB_UART_LOG_OPEN, NULL, stark_lab_uart_baud(baud_index), (uint32_t)fmt_index);
    return STARK_OK;
}

stark_err_t lab_uartsvc_tx_enable(lab_uart_ses_t *s)
{
    if (!s->open) {
        return STARK_ERR_STATE;
    }
    if (s->tx) {
        return STARK_OK;
    }
    stark_err_t err = s->io.pin_arm(s->io.ctx, s->tx_pin);
    if (err != STARK_OK) {
        return err;
    }
    /* NOTE: routing TX to its pad means installing the driver again; the ring and the counters
     * stay, and a byte that arrives in the gap is lost (the arm is taken in the Send and Loopback
     * views, where nothing is being watched). */
    s->io.close(s->io.ctx);
    err = s->io.open(s->io.ctx, stark_lab_uart_baud(s->baud_i), (uint8_t)s->fmt_i,
                     LAB_UARTSVC_RXBUF, true);
    if (err != STARK_OK) {
        return fail(s, err);
    }
    s->tx = true;
    say(s, LAB_UART_LOG_TX_ON, NULL, 0, 0);
    return STARK_OK;
}

stark_err_t lab_uartsvc_set_format(lab_uart_ses_t *s, size_t baud_index, size_t fmt_index)
{
    if (!s->open) {
        return STARK_ERR_STATE;
    }
    if (baud_index >= LAB_UART_BAUDS || fmt_index >= LAB_UART_FORMATS) {
        return STARK_ERR_INVALID_ARG;
    }
    uint32_t baud = stark_lab_uart_baud(baud_index);
    stark_err_t err;
    if (fmt_index == s->fmt_i) {
        err = s->io.set_baud(s->io.ctx, baud);
    } else {
        s->io.close(s->io.ctx);
        err = s->io.open(s->io.ctx, baud, (uint8_t)fmt_index, LAB_UARTSVC_RXBUF, s->tx);
    }
    if (err != STARK_OK) {
        return fail(s, err);
    }
    s->baud_i = baud_index;
    s->fmt_i = fmt_index;
    dlock(s);
    lab_uart_ring_clear(&s->ring);
    zero_counters(s);
    dunlock(s);
    say(s, LAB_UART_LOG_OPEN, NULL, baud, (uint32_t)fmt_index);
    return STARK_OK;
}

/* Publishes what is pending, each kind at most every LAB_UARTSVC_NOTE_MS. */
static void notify(lab_uart_ses_t *s)
{
    if (!s->pending_data && !s->pending_err) {
        return;
    }
    uint32_t now = s->io.now_ms(s->io.ctx);
    if (s->note_valid && now - s->last_note_ms < LAB_UARTSVC_NOTE_MS) {
        return;
    }
    s->note_valid = true;
    s->last_note_ms = now;
    if (s->pending_err) {
        s->io.note(s->io.ctx, LAB_UART_NOTE_ERROR);
    }
    if (s->pending_data) {
        s->io.note(s->io.ctx, LAB_UART_NOTE_DATA);
    }
    s->pending_data = false;
    s->pending_err = false;
}

stark_err_t lab_uartsvc_poll(lab_uart_ses_t *s, uint8_t *chunk, size_t max, uint32_t timeout_ms,
                             size_t *got)
{
    if (chunk == NULL || got == NULL || max == 0u) {
        return STARK_ERR_INVALID_ARG;
    }
    *got = 0;
    if (!s->open) {
        return STARK_ERR_STATE;
    }
    stark_lab_uart_err_t e = {0};
    int n = s->io.read(s->io.ctx, chunk, max < STARK_LAB_UART_CHUNK ? max : STARK_LAB_UART_CHUNK,
                       timeout_ms, &e);
    if (n < 0) {
        return fail(s, STARK_ERR_IO);
    }
    dlock(s);
    lab_uart_ring_push(&s->ring, chunk, (size_t)n);
    s->framing += e.framing;
    s->parity += e.parity;
    s->overrun += e.overrun;
    dunlock(s);
    s->pending_data = s->pending_data || n > 0;
    s->pending_err = s->pending_err || e.framing + e.parity + e.overrun > 0u;
    *got = (size_t)n;
    notify(s);
    return STARK_OK;
}

size_t lab_uartsvc_ring_read(lab_uart_ses_t *s, size_t skip, uint8_t *out, size_t max)
{
    dlock(s);
    size_t n = s->open ? lab_uart_ring_peek(&s->ring, skip, out, max) : 0u;
    dunlock(s);
    return n;
}

void lab_uartsvc_stats(lab_uart_ses_t *s, stark_lab_uart_stats_t *out)
{
    *out = (stark_lab_uart_stats_t){0};
    dlock(s);
    if (s->open) {
        out->open = true;
        out->tx = s->tx;
        out->baud_index = s->baud_i;
        out->fmt_index = s->fmt_i;
        out->rx = s->ring.total;
        out->lost = s->ring.lost;
        out->framing = s->framing;
        out->parity = s->parity;
        out->overrun = s->overrun;
        out->buffered = s->ring.count;
    }
    dunlock(s);
}

/* Reads and drops what the driver holds, a bounded number of times. Negative: the transport failed.
 */
static int drain(lab_uart_ses_t *s)
{
    uint8_t scrap[DRAIN_CHUNK];
    stark_lab_uart_err_t e;
    for (uint32_t i = 0; i < LAB_UARTSVC_DRAIN; i++) {
        int n = s->io.read(s->io.ctx, scrap, sizeof scrap, 0, &e);
        if (n <= 0) {
            return n;
        }
    }
    return 0;
}

void lab_uartsvc_clear(lab_uart_ses_t *s)
{
    if (!s->open) {
        return;
    }
    dlock(s);
    lab_uart_ring_clear(&s->ring);
    zero_counters(s);
    dunlock(s);
    if (drain(s) < 0) {
        (void)fail(s, STARK_ERR_IO);
    }
}

/* The most a write of `n` characters may take: its wire time and a little more. */
static uint32_t write_timeout_ms(const lab_uart_ses_t *s, size_t n)
{
    return lab_uart_wire_ms(n, stark_lab_uart_baud(s->baud_i), s->fmt_i) + SEND_SLACK_MS;
}

stark_err_t lab_uartsvc_send(lab_uart_ses_t *s, const uint8_t *buf, size_t n)
{
    if (buf == NULL || n == 0u || n > STARK_LAB_UART_SEND_MAX) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!s->open || !s->tx) {
        return STARK_ERR_STATE;
    }
    if (s->io.write(s->io.ctx, buf, n, write_timeout_ms(s, n)) != (int)n) {
        return fail(s, STARK_ERR_IO);
    }
    say(s, LAB_UART_LOG_SEND, NULL, (uint32_t)n, 0);
    return STARK_OK;
}

stark_err_t lab_uartsvc_loop_test(lab_uart_ses_t *s, stark_lab_loop_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!s->open || !s->tx || s->io.cancelled(s->io.ctx)) {
        return STARK_ERR_STATE;
    }
    if (drain(s) < 0) {
        return fail(s, STARK_ERR_IO);
    }
    uint8_t pattern[STARK_LAB_UART_LOOP_LEN];
    lab_uart_loop_pattern(pattern);
    for (size_t off = 0; off < STARK_LAB_UART_LOOP_LEN; off += STARK_LAB_UART_SEND_MAX) {
        if (s->io.write(s->io.ctx, pattern + off, STARK_LAB_UART_SEND_MAX,
                        write_timeout_ms(s, STARK_LAB_UART_SEND_MAX)) !=
            (int)STARK_LAB_UART_SEND_MAX) {
            return fail(s, STARK_ERR_IO);
        }
    }
    uint8_t rx[STARK_LAB_UART_LOOP_LEN];
    size_t got = 0;
    stark_lab_uart_err_t line = {0};
    uint32_t window = lab_uart_loop_window_ms(stark_lab_uart_baud(s->baud_i), s->fmt_i);
    uint32_t start = s->io.now_ms(s->io.ctx);
    /* NOTE: a slice is at least a millisecond of the window, so the loop is bounded by it even when
     * the clock stands still. */
    for (uint32_t guard = 0; got < sizeof rx && guard <= window; guard++) {
        uint32_t elapsed = s->io.now_ms(s->io.ctx) - start;
        if (elapsed >= window) {
            break;
        }
        if (s->io.cancelled(s->io.ctx)) {
            return STARK_ERR_STATE;
        }
        uint32_t slice = window - elapsed;
        slice = slice < LAB_UARTSVC_SLICE_MS ? slice : LAB_UARTSVC_SLICE_MS;
        stark_lab_uart_err_t e = {0};
        int n = s->io.read(s->io.ctx, rx + got, sizeof rx - got, slice, &e);
        if (n < 0) {
            return fail(s, STARK_ERR_IO);
        }
        got += (size_t)n;
        line.framing += e.framing;
        line.parity += e.parity;
        line.overrun += e.overrun;
    }
    lab_uart_loop_check(rx, got, sizeof pattern, line.framing, line.parity, line.overrun, out);
    say(s, LAB_UART_LOG_LOOP, out, 0, 0);
    return STARK_OK;
}

stark_err_t lab_uartsvc_autobaud(lab_uart_ses_t *s, stark_lab_autobaud_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s->open || s->io.cancelled(s->io.ctx)) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = s->io.pin_acquire(s->io.ctx, s->rx_pin);
    if (err != STARK_OK) {
        return err;
    }
    uint32_t *ts = malloc(LAB_UARTSVC_EDGES * sizeof(uint32_t));
    size_t n = 0;
    uint32_t seen = 0;
    bool first = false;
    err = ts == NULL ? STARK_ERR_NO_MEM : s->io.pad_in(s->io.ctx, s->rx_pin);
    if (err == STARK_OK) {
        err = s->io.pad_edges(s->io.ctx, s->rx_pin, LAB_UARTSVC_AUTOBAUD_US, ts, LAB_UARTSVC_EDGES,
                              &n, &seen, &first);
    }
    if (err == STARK_OK) {
        lab_autobaud_compute(ts, n, s->io.tick_hz(s->io.ctx), out);
    }
    free(ts);
    s->io.pin_release(s->io.ctx, s->rx_pin); /* before anything opens the driver on it */
    if (err != STARK_OK) {
        return err == STARK_ERR_STATE || err == STARK_ERR_NO_MEM ? err : STARK_ERR_IO;
    }
    say(s, LAB_UART_LOG_AUTOBAUD, out, 0, 0);
    return STARK_OK;
}
