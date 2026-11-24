/*
 * test_lab_uart_svc.c — the UART tool's session (components/stark_lab/lab_uart_svc.c, TASKS.md
 * STARK-0705, ADR-0023 §7) over the real pin hold (lab_pins.c, the one the GPIO app uses), the
 * host shim of the transport's UART group (the production guard) and the test-only peer
 * (test/fake/fake_lab.c), with the model clock advancing as reads and writes wait: the pins and the
 * driver on every exit, TX off until it is armed, the ring and its loss count, errors counted,
 * the loopback at every baud and format and against a mismatched, a mute and a talking peer,
 * auto-baud over the peer's stream, and every failure the transport can report. Proves the rules,
 * never a byte on a wire (ROADMAP V1 exit 13).
 */
#include <stdio.h>
#include <string.h>
#include "fake_lab.h"
#include "lab_pins.h"
#include "lab_transport_uart_host.h"
#include "lab_uart_svc.h"
#include "unity.h"

/* HARDWARE.md §2's map: lab pins 38, 39 and slot 0's 40, 41, 42. */
static const stark_board_pins_t k_map = {
    .lab_uart_tx = 38,
    .lab_uart_rx = 39,
    .module = {{.cs = 40, .irq0 = 41, .irq1 = 42, .en = -1, .eeprom_addr = 0x50},
               {.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1, .eeprom_addr = 0x51}},
};

const stark_board_pins_t *stark_board_pins(void)
{
    return &k_map;
}

static lab_pins_t s_pins;
static lab_uart_ses_t s_ses;

#define LOG_MAX 64
static struct {
    struct {
        lab_uart_log_t what;
        uint32_t a, b;
        char text[40];
    } log[LOG_MAX];
    size_t n_logs;
    unsigned data_notes, error_notes;
    int locked;
    unsigned locks, unlocks;
    bool fail_arm, cancelled, stuck_clock;
    int cancel_after; /* cancelled() turns true after this many calls (-1: never) */
    unsigned cancel_calls;
    bool fail_pad_in, fail_edges, cancel_edges;
    bool held_tx_in_edges, held_rx_in_edges;
    unsigned edge_calls;
    size_t rxbuf; /* the driver's buffer, when a test wants a smaller one (0: as asked) */
} t;

static size_t pio_list(void *ctx, stark_module_entry_t *out, size_t max)
{
    (void)ctx;
    (void)out;
    (void)max;
    return 0; /* no module service: only the LAB pins are on offer */
}

static stark_err_t pio_pad_in(void *ctx, int pin, uint8_t pull)
{
    (void)ctx;
    return fake_lab_pad_in(pin, pull);
}

static stark_err_t pio_pad_out(void *ctx, int pin, bool level)
{
    (void)ctx;
    return fake_lab_pad_out(pin, level);
}

static stark_err_t pio_pad_read(void *ctx, int pin, bool *level)
{
    (void)ctx;
    return fake_lab_pad_read(pin, level);
}

static stark_err_t pio_pad_hiz(void *ctx, int pin)
{
    (void)ctx;
    return fake_lab_pad_hiz(pin);
}

static void pio_delay(void *ctx, uint32_t us)
{
    (void)ctx;
    (void)us;
}

static void pio_log(void *ctx, lab_log_t what, int a, int b)
{
    (void)ctx;
    (void)what;
    (void)a;
    (void)b;
}

static stark_err_t uio_pin_acquire(void *ctx, int pin)
{
    (void)ctx;
    return lab_pins_acquire(&s_pins, pin, "uart");
}

static void uio_pin_release(void *ctx, int pin)
{
    (void)ctx;
    lab_pins_release(&s_pins, pin);
}

static stark_err_t uio_pin_arm(void *ctx, int pin)
{
    (void)ctx;
    return t.fail_arm ? STARK_ERR_STATE : lab_pins_arm(&s_pins, pin);
}

static stark_err_t uio_pad_in(void *ctx, int pin)
{
    (void)ctx;
    return t.fail_pad_in ? STARK_ERR_IO : lab_pins_input(&s_pins, pin, STARK_LAB_PULL_NONE);
}

static bool held(int pin)
{
    stark_lab_pin_t list[8];
    size_t n = lab_pins_list(&s_pins, list, 8);
    for (size_t i = 0; i < n; i++) {
        if (list[i].pin == pin) {
            return list[i].held;
        }
    }
    return false;
}

static stark_err_t uio_pad_edges(void *ctx, int pin, uint32_t window_us, uint32_t *ts, size_t cap,
                                 size_t *n, uint32_t *seen, bool *first_level)
{
    (void)ctx;
    t.edge_calls++;
    t.held_tx_in_edges = held(38);
    t.held_rx_in_edges = held(39);
    if (t.cancel_edges) {
        return STARK_ERR_STATE;
    }
    if (t.fail_edges) {
        return STARK_ERR_IO;
    }
    return fake_lab_pad_edges(pin, window_us, ts, cap, n, seen, first_level);
}

static uint32_t uio_tick_hz(void *ctx)
{
    (void)ctx;
    return FAKE_LAB_TICK_HZ;
}

static stark_err_t uio_open(void *ctx, uint32_t baud, uint8_t fmt, size_t rx_buf, bool tx)
{
    (void)ctx;
    return stark_lab_transport_uart_open(baud, fmt, t.rxbuf != 0u ? t.rxbuf : rx_buf, tx);
}

static stark_err_t uio_set_baud(void *ctx, uint32_t baud)
{
    (void)ctx;
    return stark_lab_transport_uart_set_baud(baud);
}

static int uio_read(void *ctx, uint8_t *buf, size_t max, uint32_t timeout_ms,
                    stark_lab_uart_err_t *errs)
{
    (void)ctx;
    return stark_lab_transport_uart_read(buf, max, timeout_ms, errs);
}

static int uio_write(void *ctx, const uint8_t *buf, size_t n, uint32_t timeout_ms)
{
    (void)ctx;
    return stark_lab_transport_uart_write(buf, n, timeout_ms);
}

static void uio_close(void *ctx)
{
    (void)ctx;
    stark_lab_transport_uart_close();
}

static uint32_t uio_now_ms(void *ctx)
{
    (void)ctx;
    return t.stuck_clock ? 5000u : (uint32_t)(lab_uart_host_time_us() / 1000u);
}

static bool uio_cancelled(void *ctx)
{
    (void)ctx;
    if (t.cancel_after >= 0 && t.cancel_calls++ >= (unsigned)t.cancel_after) {
        return true;
    }
    return t.cancelled;
}

static void uio_lock(void *ctx)
{
    (void)ctx;
    t.locked++;
    t.locks++;
}

static void uio_unlock(void *ctx)
{
    (void)ctx;
    t.locked--;
    t.unlocks++;
}

static void uio_log(void *ctx, lab_uart_log_t what, const void *p, uint32_t a, uint32_t b)
{
    (void)ctx;
    if (t.n_logs >= LOG_MAX) {
        return;
    }
    size_t i = t.n_logs++;
    t.log[i].what = what;
    t.log[i].a = a;
    t.log[i].b = b;
    t.log[i].text[0] = '\0';
    if (what == LAB_UART_LOG_LOOP) {
        stark_lab_loop_log_text(p, t.log[i].text, sizeof t.log[i].text);
    } else if (what == LAB_UART_LOG_AUTOBAUD) {
        char num[16];
        snprintf(t.log[i].text, sizeof t.log[i].text, "%s",
                 stark_lab_autobaud_text(p, num, sizeof num));
    }
}

static void uio_note(void *ctx, lab_uart_note_t what)
{
    (void)ctx;
    if (what == LAB_UART_NOTE_ERROR) {
        t.error_notes++;
    } else {
        t.data_notes++;
    }
}

static lab_uart_io_t make_io(void)
{
    return (lab_uart_io_t){.pin_acquire = uio_pin_acquire,
                           .pin_release = uio_pin_release,
                           .pin_arm = uio_pin_arm,
                           .pad_in = uio_pad_in,
                           .pad_edges = uio_pad_edges,
                           .tick_hz = uio_tick_hz,
                           .open = uio_open,
                           .set_baud = uio_set_baud,
                           .read = uio_read,
                           .write = uio_write,
                           .close = uio_close,
                           .now_ms = uio_now_ms,
                           .cancelled = uio_cancelled,
                           .data_lock = uio_lock,
                           .data_unlock = uio_unlock,
                           .log = uio_log,
                           .note = uio_note};
}

void setUp(void)
{
    memset(&t, 0, sizeof t);
    t.cancel_after = -1;
    fake_lab_reset();
    lab_uart_host_reset();
    const lab_io_t pio = {.list = pio_list,
                          .pad_in = pio_pad_in,
                          .pad_out = pio_pad_out,
                          .pad_read = pio_pad_read,
                          .pad_hiz = pio_pad_hiz,
                          .delay_us = pio_delay,
                          .log = pio_log};
    lab_pins_init(&s_pins, &pio, &k_map);
    const lab_uart_io_t uio = make_io();
    lab_uartsvc_init(&s_ses, &uio, &k_map);
}

/* Every test ends with nothing installed and nothing held, and the data lock balanced. */
void tearDown(void)
{
    lab_uartsvc_close(&s_ses);
    TEST_ASSERT_EQUAL_UINT(lab_uart_host_opens(), lab_uart_host_closes());
    TEST_ASSERT_FALSE(fake_lab_uart_is_open());
    TEST_ASSERT_FALSE(held(38));
    TEST_ASSERT_FALSE(held(39));
    TEST_ASSERT_EQUAL_INT(0, t.locked);
    TEST_ASSERT_EQUAL_UINT(t.locks, t.unlocks);
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(39));
}

/* ---- helpers --------------------------------------------------------------------------------- */

#define B115200 7u /* indices into the baud list */
#define B9600   3u
#define B1200   0u
#define B38400  5u
#define F8N1    0u
#define F8E1    1u

static void open_ok(size_t baud, size_t fmt)
{
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_open(&s_ses, baud, fmt));
}

static void open_tx(size_t baud, size_t fmt)
{
    open_ok(baud, fmt);
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_tx_enable(&s_ses));
}

static bool log_has(lab_uart_log_t what)
{
    for (size_t i = 0; i < t.n_logs; i++) {
        if (t.log[i].what == what) {
            return true;
        }
    }
    return false;
}

static unsigned log_count(lab_uart_log_t what)
{
    unsigned n = 0;
    for (size_t i = 0; i < t.n_logs; i++) {
        n += t.log[i].what == what ? 1u : 0u;
    }
    return n;
}

static const char *last_text(lab_uart_log_t what)
{
    for (size_t i = t.n_logs; i > 0; i--) {
        if (t.log[i - 1u].what == what) {
            return t.log[i - 1u].text;
        }
    }
    return "";
}

/* After a failure that ends the session: no driver, no pins, the ring gone. */
static void assert_ended(void)
{
    stark_lab_uart_stats_t st;
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_FALSE(st.open);
    TEST_ASSERT_FALSE(held(38));
    TEST_ASSERT_FALSE(held(39));
    TEST_ASSERT_FALSE(fake_lab_uart_is_open());
    TEST_ASSERT_EQUAL_UINT(lab_uart_host_opens(), lab_uart_host_closes());
}

/* Polls (10 ms waits) until `want` bytes have come or `rounds` polls passed; returns the total. */
static size_t poll_until(size_t want, unsigned rounds, uint8_t *copy)
{
    size_t total = 0;
    uint8_t chunk[STARK_LAB_UART_CHUNK];
    for (unsigned i = 0; i < rounds && total < want; i++) {
        size_t got = 0;
        TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_poll(&s_ses, chunk, sizeof chunk, 10, &got));
        if (copy != NULL && total + got <= want) {
            memcpy(copy + total, chunk, got);
        }
        total += got;
    }
    return total;
}

/* ---- open, close, the pins --------------------------------------------------------------------
 */

static void test_open_holds_both_pins_and_closing_gives_everything_back(void)
{
    open_ok(B115200, F8N1);
    stark_lab_uart_stats_t st;
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_TRUE(st.open);
    TEST_ASSERT_FALSE(st.tx);
    TEST_ASSERT_EQUAL_UINT(B115200, st.baud_index);
    TEST_ASSERT_EQUAL_UINT(F8N1, st.fmt_index);
    TEST_ASSERT_TRUE(held(38));
    TEST_ASSERT_TRUE(held(39));
    TEST_ASSERT_TRUE(fake_lab_uart_is_open());
    TEST_ASSERT_FALSE(fake_lab_uart_tx()); /* a monitor drives nothing */
    TEST_ASSERT_EQUAL_UINT(1, lab_uart_host_opens());
    TEST_ASSERT_EQUAL(LAB_UART_LOG_OPEN, t.log[0].what);
    TEST_ASSERT_EQUAL_UINT32(115200, t.log[0].a);
    TEST_ASSERT_EQUAL_UINT32(0, t.log[0].b);
    /* open twice is a state error and costs nothing */
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_open(&s_ses, B9600, F8N1));
    TEST_ASSERT_EQUAL_UINT(1, lab_uart_host_opens());
    /* AC 4: while the tool holds the pins, nothing else can take them */
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, lab_pins_acquire(&s_pins, 38, "gpio"));
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, lab_pins_acquire(&s_pins, 39, "gpio"));

    lab_uartsvc_close(&s_ses);
    TEST_ASSERT_EQUAL_UINT(1, lab_uart_host_closes());
    TEST_ASSERT_EQUAL_UINT(1, log_count(LAB_UART_LOG_CLOSED));
    assert_ended();
    lab_uartsvc_close(&s_ses); /* idempotent: no second log line, no second removal */
    TEST_ASSERT_EQUAL_UINT(1, log_count(LAB_UART_LOG_CLOSED));
    TEST_ASSERT_EQUAL_UINT(1, lab_uart_host_closes());
    /* and the GPIO app can have them again */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_pins, 38, "gpio"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_pins, 39, "gpio"));
    lab_pins_release_all(&s_pins);
}

static void test_open_refuses_what_it_cannot_do_and_holds_nothing(void)
{
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_uartsvc_open(&s_ses, STARK_LAB_UART_BAUDS, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_uartsvc_open(&s_ses, 0, STARK_LAB_UART_FMTS));
    assert_ended();
    TEST_ASSERT_EQUAL_UINT(0, lab_uart_host_opens());

    /* the GPIO app has the RX pin: the tool refuses and leaves the TX pin free */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_pins, 39, "gpio"));
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, lab_uartsvc_open(&s_ses, B115200, F8N1));
    TEST_ASSERT_FALSE(held(38));
    TEST_ASSERT_TRUE(held(39)); /* still the GPIO app's */
    lab_pins_release(&s_pins, 39);
    /* ... or the TX pin */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_pins, 38, "gpio"));
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, lab_uartsvc_open(&s_ses, B115200, F8N1));
    TEST_ASSERT_FALSE(held(39));
    lab_pins_release(&s_pins, 38);
    TEST_ASSERT_EQUAL_UINT(0, lab_uart_host_opens());

    /* the driver refuses: both pins are given back */
    lab_uart_host_fail(LAB_UART_HOST_OPEN, 0);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_uartsvc_open(&s_ses, B115200, F8N1));
    assert_ended();
    lab_uart_host_fail(LAB_UART_HOST_OPEN, -1);
    open_ok(B115200, F8N1); /* and a later open works */
}

/* ---- TX is off until it is armed --------------------------------------------------------------
 */

static void test_nothing_is_sent_until_tx_is_armed(void)
{
    const uint8_t b[] = {1, 2, 3};
    stark_lab_loop_t r;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_tx_enable(&s_ses)); /* closed */
    open_ok(B115200, F8N1);
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_send(&s_ses, b, sizeof b));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_loop_test(&s_ses, &r));
    stark_lab_info_t info;
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_info(&s_pins, 38, &info));
    TEST_ASSERT_FALSE(info.armed);

    /* the app's confirmation arrives: the pin is armed and TX is routed to the pad */
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_tx_enable(&s_ses));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_info(&s_pins, 38, &info));
    TEST_ASSERT_TRUE(info.armed);
    TEST_ASSERT_TRUE(fake_lab_uart_tx());
    TEST_ASSERT_EQUAL_UINT(2, lab_uart_host_opens()); /* TX means installing the driver again */
    TEST_ASSERT_EQUAL_UINT(1, lab_uart_host_closes());
    TEST_ASSERT_EQUAL_UINT(1, log_count(LAB_UART_LOG_TX_ON));
    stark_lab_uart_stats_t st;
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_TRUE(st.tx);
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_tx_enable(&s_ses)); /* idempotent */
    TEST_ASSERT_EQUAL_UINT(2, lab_uart_host_opens());

    lab_uartsvc_close(&s_ses);
    /* leaving clears the arm with the hold: the next session is receive-only again */
    open_ok(B115200, F8N1);
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_send(&s_ses, b, sizeof b));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_info(&s_pins, 38, &info));
    TEST_ASSERT_FALSE(info.armed);
}

static void test_a_refused_arm_or_a_failed_reinstall(void)
{
    open_ok(B115200, F8N1);
    t.fail_arm = true;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_tx_enable(&s_ses));
    stark_lab_uart_stats_t st;
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_TRUE(st.open); /* nothing changed */
    TEST_ASSERT_FALSE(st.tx);
    TEST_ASSERT_TRUE(held(38));
    t.fail_arm = false;
    lab_uart_host_fail(LAB_UART_HOST_OPEN, 0); /* the reinstall with TX fails */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_uartsvc_tx_enable(&s_ses));
    assert_ended();
    TEST_ASSERT_TRUE(log_has(LAB_UART_LOG_FAILED));
}

/* ---- a new baud or format ---------------------------------------------------------------------
 */

static void test_a_new_baud_or_format_clears_the_ring_and_keeps_tx(void)
{
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_set_format(&s_ses, B9600, F8N1));
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 115200, F8N1);
    open_tx(B115200, F8N1);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_uartsvc_set_format(&s_ses, 11, F8N1));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_uartsvc_set_format(&s_ses, B9600, 5));
    TEST_ASSERT_TRUE(poll_until(100, 20, NULL) >= 100u);
    stark_lab_uart_stats_t st;
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_TRUE(st.buffered >= 100u);
    unsigned opens = lab_uart_host_opens();

    /* only the baud changes: the driver stays, its baud moves */
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_set_format(&s_ses, B9600, F8N1));
    TEST_ASSERT_EQUAL_UINT(opens, lab_uart_host_opens());
    TEST_ASSERT_EQUAL_UINT32(1042, fake_lab_uart_wire_us(1)); /* one 8N1 character at 9600 */
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_EQUAL_UINT(0, st.buffered);
    TEST_ASSERT_EQUAL_UINT32(0, st.rx);
    TEST_ASSERT_EQUAL_UINT(B9600, st.baud_index);
    TEST_ASSERT_TRUE(st.tx);
    TEST_ASSERT_EQUAL(LAB_UART_LOG_OPEN, t.log[t.n_logs - 1u].what);
    TEST_ASSERT_EQUAL_UINT32(9600, t.log[t.n_logs - 1u].a);

    /* the format changes: the driver is installed again, TX still routed */
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_set_format(&s_ses, B9600, 4));
    TEST_ASSERT_EQUAL_UINT(opens + 1u, lab_uart_host_opens());
    TEST_ASSERT_TRUE(fake_lab_uart_tx());
    TEST_ASSERT_EQUAL_UINT32(1042, fake_lab_uart_wire_us(1)); /* 7E1 is ten bits too */
    TEST_ASSERT_EQUAL_UINT32(2084, fake_lab_uart_wire_us(2));
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_EQUAL_UINT(4, st.fmt_index);
}

static void test_a_failed_change_ends_the_session(void)
{
    open_ok(B115200, F8N1);
    lab_uart_host_fail(LAB_UART_HOST_SET_BAUD, 0);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_uartsvc_set_format(&s_ses, B9600, F8N1));
    assert_ended();
    lab_uart_host_fail(LAB_UART_HOST_SET_BAUD, -1);
    open_ok(B115200, F8N1);
    lab_uart_host_fail(LAB_UART_HOST_OPEN, 0);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_uartsvc_set_format(&s_ses, B9600, F8E1));
    assert_ended();
}

/* ---- the monitor: poll, ring, counters ---------------------------------------------------------
 */

static void test_poll_fills_the_ring_with_what_the_peer_said(void)
{
    uint8_t chunk[8];
    size_t got = 0;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_uartsvc_poll(&s_ses, NULL, 8, 10, &got));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_uartsvc_poll(&s_ses, chunk, 0, 10, &got));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_uartsvc_poll(&s_ses, chunk, 8, 10, NULL));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_poll(&s_ses, chunk, 8, 10, &got));
    TEST_ASSERT_EQUAL_UINT(0, got);

    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 115200, F8N1);
    open_ok(B115200, F8N1);
    size_t scene = fake_lab_uart_scene_len(FAKE_LAB_UART_TEXT);
    uint8_t copy[1300];
    size_t total = poll_until(scene, 400, copy);
    TEST_ASSERT_EQUAL_UINT(scene, total);
    for (size_t j = 0; j < scene; j++) {
        TEST_ASSERT_EQUAL_UINT8(fake_lab_uart_scene_byte(FAKE_LAB_UART_TEXT, j), copy[j]);
    }
    uint8_t ring[1300];
    TEST_ASSERT_EQUAL_UINT(scene, lab_uartsvc_ring_read(&s_ses, 0, ring, sizeof ring));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(copy, ring, scene);
    TEST_ASSERT_EQUAL_UINT(10, lab_uartsvc_ring_read(&s_ses, 1290, ring, sizeof ring));
    TEST_ASSERT_EQUAL_UINT(0, lab_uartsvc_ring_read(&s_ses, 1300, ring, sizeof ring));
    stark_lab_uart_stats_t st;
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_EQUAL_UINT32(scene, st.rx);
    TEST_ASSERT_EQUAL_UINT(scene, st.buffered);
    TEST_ASSERT_EQUAL_UINT32(0, st.lost);
    TEST_ASSERT_EQUAL_UINT32(0, st.framing + st.parity + st.overrun);
    /* events: a few a second at most, never one per poll */
    TEST_ASSERT_TRUE(t.data_notes >= 1u);
    TEST_ASSERT_TRUE(t.data_notes <= lab_uart_host_time_us() / 100000u + 1u);
    TEST_ASSERT_EQUAL_UINT(0, t.error_notes);

    /* a poll asks for no more than the chunk or the caller's room */
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 115200, F8N1);
    lab_uartsvc_close(&s_ses);
    open_ok(B115200, F8N1);
    lab_uart_host_set_time_us(lab_uart_host_time_us() + 500000u);
    uint8_t big[200];
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_poll(&s_ses, big, sizeof big, 0, &got));
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_UART_CHUNK, got);
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_poll(&s_ses, big, 10, 0, &got));
    TEST_ASSERT_EQUAL_UINT(10, got);
}

/* AC 3: the ring holds 4 kB and everything it overwrites is counted. */
static void test_a_full_ring_counts_what_it_overwrites(void)
{
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 115200, F8N1);
    open_ok(B115200, F8N1);
    uint8_t junk[1000];
    memset(junk, 0xEE, sizeof junk);
    for (int i = 0; i < 4; i++) {
        lab_uart_ring_push(&s_ses.ring, junk, sizeof junk);
    }
    size_t scene = fake_lab_uart_scene_len(FAKE_LAB_UART_TEXT);
    TEST_ASSERT_EQUAL_UINT(scene, poll_until(scene, 400, NULL));
    stark_lab_uart_stats_t st;
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_EQUAL_UINT(4096, st.buffered);
    TEST_ASSERT_EQUAL_UINT32(4000u + scene, st.rx);
    TEST_ASSERT_EQUAL_UINT32(4000u + scene - 4096u, st.lost);
    /* the newest bytes are the scene's end */
    uint8_t tail[4];
    TEST_ASSERT_EQUAL_UINT(4, lab_uartsvc_ring_read(&s_ses, 4092, tail, sizeof tail));
    TEST_ASSERT_EQUAL_UINT8('\r', tail[2]);
    TEST_ASSERT_EQUAL_UINT8('\n', tail[3]);
}

static void test_errors_are_counted_and_announced(void)
{
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 9600, F8N1); /* the peer is slower than the tool */
    open_ok(B115200, F8N1);
    size_t got = poll_until(120, 400, NULL);
    TEST_ASSERT_TRUE(got >= 120u);
    stark_lab_uart_stats_t st;
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_EQUAL_UINT32(st.rx, st.framing); /* every character arrived as a framing error */
    TEST_ASSERT_EQUAL_UINT32(0, st.parity);
    TEST_ASSERT_TRUE(t.error_notes >= 1u);
    TEST_ASSERT_TRUE(t.error_notes <= lab_uart_host_time_us() / 100000u + 1u);

    lab_uartsvc_close(&s_ses);
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 115200, F8E1); /* the format differs: parity errors */
    open_ok(B115200, F8N1);
    lab_uart_host_set_time_us(lab_uart_host_time_us() + 100000u);
    uint8_t scrap[64];
    size_t n = 0;
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_poll(&s_ses, scrap, sizeof scrap, 0, &n));
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_EQUAL_UINT32(0, st.parity); /* 8N1 checks no parity: it sees a framing error */
    TEST_ASSERT_TRUE(st.framing > 0u);
    lab_uartsvc_close(&s_ses);
    open_ok(B115200, F8E1);
    lab_uart_host_set_time_us(lab_uart_host_time_us() + 100000u);
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 115200, F8N1);
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_poll(&s_ses, scrap, sizeof scrap, 0, &n));
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_TRUE(st.parity > 0u); /* an 8E1 receiver checks parity */
}

static void test_a_buffer_that_overflows_is_an_overrun(void)
{
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 921600, F8N1);
    t.rxbuf = STARK_LAB_UART_RXBUF_MIN; /* a driver buffer smaller than the scene */
    open_ok(B115200 + 3u, F8N1);        /* the tool listens at 921 600 */
    lab_uart_host_set_time_us(lab_uart_host_time_us() + 200000u); /* 200 ms, nobody reading */
    uint8_t chunk[64];
    size_t got = 0;
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_poll(&s_ses, chunk, sizeof chunk, 0, &got));
    stark_lab_uart_stats_t st;
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_TRUE(st.overrun >= 1u); /* the driver's buffer overflowed */
    TEST_ASSERT_TRUE(t.error_notes >= 1u);
}

static void test_a_failed_read_ends_the_session(void)
{
    open_ok(B115200, F8N1);
    lab_uart_host_fail(LAB_UART_HOST_READ, 0);
    uint8_t chunk[8];
    size_t got = 5;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_uartsvc_poll(&s_ses, chunk, sizeof chunk, 10, &got));
    TEST_ASSERT_EQUAL_UINT(0, got);
    assert_ended();
    TEST_ASSERT_TRUE(log_has(LAB_UART_LOG_FAILED));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_poll(&s_ses, chunk, sizeof chunk, 10, &got));
    /* a ring read or the counters after the end are empty, never stale */
    TEST_ASSERT_EQUAL_UINT(0, lab_uartsvc_ring_read(&s_ses, 0, chunk, sizeof chunk));
    stark_lab_uart_stats_t st;
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_EQUAL_UINT32(0, st.rx);
}

static void test_clear_empties_the_ring_the_counters_and_the_driver(void)
{
    lab_uartsvc_clear(&s_ses); /* closed: nothing to do */
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 9600, F8N1); /* errors, so the counters have a value */
    open_ok(B115200, F8N1);
    (void)poll_until(40, 400, NULL);
    lab_uart_host_set_time_us(lab_uart_host_time_us() +
                              20000u); /* ~20 characters wait in the driver */
    lab_uartsvc_clear(&s_ses);
    stark_lab_uart_stats_t st;
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_EQUAL_UINT32(0, st.rx);
    TEST_ASSERT_EQUAL_UINT(0, st.buffered);
    TEST_ASSERT_EQUAL_UINT32(0, st.framing);
    uint8_t chunk[64];
    size_t got = 9;
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_poll(&s_ses, chunk, sizeof chunk, 0, &got));
    TEST_ASSERT_EQUAL_UINT(0, got); /* what was waiting is gone */
    /* a failing driver while it drains ends the session */
    lab_uart_host_fail(LAB_UART_HOST_READ, 0);
    lab_uartsvc_clear(&s_ses);
    assert_ended();
}

/* ---- send -------------------------------------------------------------------------------------
 */

static void test_send_goes_out_once_and_only_within_its_bounds(void)
{
    uint8_t data[40];
    memset(data, 'x', sizeof data);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_uartsvc_send(&s_ses, NULL, 4));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_uartsvc_send(&s_ses, data, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_uartsvc_send(&s_ses, data, 33));
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_ECHO, 115200, F8N1);
    open_tx(B115200, F8N1);
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_send(&s_ses, (const uint8_t *)"AT\r\n", 4));
    TEST_ASSERT_EQUAL_UINT(1, log_count(LAB_UART_LOG_SEND));
    TEST_ASSERT_EQUAL_UINT32(4, t.log[t.n_logs - 1u].a);
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_send(&s_ses, data, 32)); /* the most */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_uartsvc_send(&s_ses, data, 33));
    /* the peer echoed exactly what was sent, nothing more */
    TEST_ASSERT_EQUAL_UINT(36, poll_until(36, 100, NULL));
    uint8_t ring[40];
    TEST_ASSERT_EQUAL_UINT(36, lab_uartsvc_ring_read(&s_ses, 0, ring, sizeof ring));
    TEST_ASSERT_EQUAL_UINT8_ARRAY("AT\r\n", ring, 4);
    TEST_ASSERT_EQUAL_UINT8('x', ring[35]);
    uint8_t chunk[8];
    size_t got = 0;
    lab_uart_host_set_time_us(lab_uart_host_time_us() + 1000000u);
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_poll(&s_ses, chunk, sizeof chunk, 0, &got));
    TEST_ASSERT_EQUAL_UINT(0, got); /* a send is one send */
}

static void test_a_slow_line_still_sends_32_bytes(void)
{
    uint8_t data[32];
    memset(data, 'y', sizeof data);
    fake_lab_uart_pins(38, 39);
    open_tx(B1200, F8N1);
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_send(&s_ses, data, sizeof data)); /* 267 ms of wire */
    TEST_ASSERT_TRUE(lab_uart_host_time_us() >= 266000u);
}

static void test_a_failed_write_ends_the_session(void)
{
    uint8_t data[4] = {1, 2, 3, 4};
    open_tx(B115200, F8N1);
    lab_uart_host_fail(LAB_UART_HOST_WRITE, 0);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_uartsvc_send(&s_ses, data, sizeof data));
    assert_ended();
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_send(&s_ses, data, sizeof data));
}

/* ---- the loopback bridge ----------------------------------------------------------------------
 */

/* AC 1: the 64-byte pattern comes back whole and without errors at every baud and format. */
static void test_the_loopback_round_trips_at_every_baud_and_format(void)
{
    for (size_t f = 0; f < STARK_LAB_UART_FMTS; f++) {
        for (size_t b = 0; b < STARK_LAB_UART_BAUDS; b++) {
            fake_lab_reset();
            lab_uart_host_reset();
            fake_lab_uart_pins(38, 39);
            fake_lab_uart_peer(FAKE_LAB_UART_LOOP, 0, 0);
            open_tx(b, f);
            stark_lab_loop_t r;
            char msg[40];
            snprintf(msg, sizeof msg, "%u %s", (unsigned)stark_lab_uart_baud(b),
                     stark_lab_uart_fmt_name(f));
            TEST_ASSERT_EQUAL_MESSAGE(STARK_OK, lab_uartsvc_loop_test(&s_ses, &r), msg);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(STARK_LAB_LOOP_OK, (unsigned)r.status, msg);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(64, r.sent, msg);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(64, r.received, msg);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.errors, msg);
            TEST_ASSERT_EQUAL_INT32_MESSAGE(-1, r.first_mismatch, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("64/64 ok", last_text(LAB_UART_LOG_LOOP), msg);
            /* it took the wire time of the pattern, and nowhere near its whole window */
            uint32_t wire = lab_uart_wire_ms(64, stark_lab_uart_baud(b), f);
            TEST_ASSERT_TRUE_MESSAGE(lab_uart_host_time_us() / 1000u + 1u >= wire, msg);
            TEST_ASSERT_TRUE_MESSAGE(
                lab_uart_host_time_us() / 1000u <= wire + 2u * LAB_UARTSVC_SLICE_MS + 2u, msg);
            lab_uartsvc_close(&s_ses);
            t.n_logs = 0;
        }
    }
}

static void test_a_peer_that_echoes_correctly_passes(void)
{
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_ECHO, 115200, F8N1);
    open_tx(B115200, F8N1);
    stark_lab_loop_t r;
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_loop_test(&s_ses, &r));
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_LOOP_OK, (unsigned)r.status);
    lab_uartsvc_close(&s_ses);
    /* 1.6 % off is inside the 3 % the model tolerates */
    fake_lab_uart_peer(FAKE_LAB_UART_ECHO, 117000, F8N1);
    open_tx(B115200, F8N1);
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_loop_test(&s_ses, &r));
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_LOOP_OK, (unsigned)r.status);
}

/* AC 1: a mismatch is reported as errors and never as success. */
static void test_a_mismatched_peer_is_reported_with_its_errors(void)
{
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_ECHO, 9600, F8N1); /* the tool is at 115 200 */
    open_tx(B115200, F8N1);
    stark_lab_loop_t r;
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_loop_test(&s_ses, &r));
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_LOOP_MISMATCH, (unsigned)r.status);
    TEST_ASSERT_EQUAL_INT32(0, r.first_mismatch);
    TEST_ASSERT_TRUE(r.received > 0u);
    TEST_ASSERT_TRUE(r.framing > 0u);
    TEST_ASSERT_TRUE(r.errors >= r.framing);
    /* the slow peer's echo is still crossing the wire when the window ends: not all of it is back
     */
    TEST_ASSERT_NOT_NULL(strstr(last_text(LAB_UART_LOG_LOOP), "/64 mismatch first 0"));
    lab_uartsvc_close(&s_ses);

    /* the format differs: parity errors on the side that checks parity */
    fake_lab_uart_peer(FAKE_LAB_UART_ECHO, 115200, F8N1);
    open_tx(B115200, F8E1);
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_loop_test(&s_ses, &r));
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_LOOP_MISMATCH, (unsigned)r.status);
    TEST_ASSERT_TRUE(r.parity > 0u);
    lab_uartsvc_close(&s_ses);

    /* a peer that is talking is not a loopback */
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 115200, F8N1);
    open_tx(B115200, F8N1);
    lab_uart_host_set_time_us(lab_uart_host_time_us() + 3000u);
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_loop_test(&s_ses, &r));
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_LOOP_MISMATCH, (unsigned)r.status);
}

static void test_a_mute_peer_is_no_data_after_the_whole_window(void)
{
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_SILENT, 0, 0);
    open_tx(B9600, F8N1);
    uint64_t before = lab_uart_host_time_us();
    stark_lab_loop_t r;
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_loop_test(&s_ses, &r));
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_LOOP_NO_DATA, (unsigned)r.status);
    TEST_ASSERT_EQUAL_UINT32(0, r.received);
    TEST_ASSERT_EQUAL_UINT32(64, r.sent);
    TEST_ASSERT_EQUAL_STRING("0/64 no data", last_text(LAB_UART_LOG_LOOP));
    /* it listened for the window and no longer: two wire times and 20 ms after the writes */
    uint64_t took_ms = (lab_uart_host_time_us() - before) / 1000u;
    uint32_t wire = lab_uart_wire_ms(64, 9600, F8N1);
    uint32_t window = lab_uart_loop_window_ms(9600, F8N1);
    TEST_ASSERT_TRUE(took_ms + 1u >= wire + window);
    TEST_ASSERT_TRUE(took_ms <= wire + window + 2u);
}

static void test_bytes_left_over_from_a_send_are_flushed_first(void)
{
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_LOOP, 0, 0);
    open_tx(B115200, F8N1);
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_send(&s_ses, (const uint8_t *)"stale", 5));
    stark_lab_loop_t r;
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_loop_test(&s_ses, &r));
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_LOOP_OK, (unsigned)r.status);
    TEST_ASSERT_EQUAL_UINT32(64, r.received);
}

static void test_the_loopback_can_be_cancelled_and_never_hangs(void)
{
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_SILENT, 0, 0);
    open_tx(B9600, F8N1);
    stark_lab_loop_t r;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_uartsvc_loop_test(&s_ses, NULL));
    t.cancelled = true; /* asked to stop before it starts: nothing is sent */
    uint64_t before = lab_uart_host_time_us();
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_loop_test(&s_ses, &r));
    TEST_ASSERT_EQUAL_UINT64(before, lab_uart_host_time_us());
    t.cancelled = false;
    t.cancel_after = 3; /* asked while it listens */
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_loop_test(&s_ses, &r));
    stark_lab_uart_stats_t st;
    lab_uartsvc_stats(&s_ses, &st);
    TEST_ASSERT_TRUE(st.open); /* a cancel keeps the session */
    TEST_ASSERT_EQUAL_UINT(0, log_count(LAB_UART_LOG_LOOP));
    t.cancel_after = -1;
    /* a clock that stands still cannot hold it: the window bounds the number of waits */
    t.stuck_clock = true;
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_loop_test(&s_ses, &r));
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_LOOP_NO_DATA, (unsigned)r.status);
}

static void test_a_failing_driver_ends_a_loopback(void)
{
    stark_lab_loop_t r;
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_LOOP, 0, 0);
    open_tx(B115200, F8N1);
    lab_uart_host_fail(LAB_UART_HOST_READ, 0); /* the drain */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_uartsvc_loop_test(&s_ses, &r));
    assert_ended();
    lab_uart_host_fail(LAB_UART_HOST_READ, -1);

    open_tx(B115200, F8N1);
    lab_uart_host_fail(LAB_UART_HOST_WRITE, 0); /* the first half of the pattern */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_uartsvc_loop_test(&s_ses, &r));
    assert_ended();
    lab_uart_host_fail(LAB_UART_HOST_WRITE, -1);

    open_tx(B115200, F8N1);
    lab_uart_host_fail(LAB_UART_HOST_WRITE, 1); /* the second half */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_uartsvc_loop_test(&s_ses, &r));
    assert_ended();
    lab_uart_host_fail(LAB_UART_HOST_WRITE, -1);

    open_tx(B115200, F8N1);
    lab_uart_host_fail(LAB_UART_HOST_READ, 1); /* the listen: the drain read was the first */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_uartsvc_loop_test(&s_ses, &r));
    assert_ended();
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_loop_test(&s_ses, &r));
}

static void test_a_drained_stream_does_not_keep_the_flush_going_forever(void)
{
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_BINARY, 921600, F8N1);
    open_tx(B115200 + 3u, F8N1);
    lab_uart_host_set_time_us(lab_uart_host_time_us() + 30000u); /* more than 8 chunks wait */
    stark_lab_loop_t r;
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_loop_test(&s_ses, &r));
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_LOOP_MISMATCH, (unsigned)r.status);
}

/* ---- auto-baud --------------------------------------------------------------------------------
 */

/* AC 2: the baud of the peer's stream, at every baud and format, within 3 %. */
static void test_autobaud_names_the_peer_at_every_baud_and_format(void)
{
    for (uint8_t f = 0; f < STARK_LAB_UART_FMTS; f++) {
        for (size_t b = 0; b < STARK_LAB_UART_BAUDS; b++) {
            uint32_t baud = stark_lab_uart_baud(b);
            fake_lab_reset();
            lab_uart_host_reset();
            t.n_logs = 0;
            fake_lab_uart_pins(38, 39);
            fake_lab_uart_peer(FAKE_LAB_UART_TEXT, baud, f);
            uint32_t jitter = 240000000u / baud / 10u;
            fake_lab_edge_noise(jitter < 240u ? jitter : 240u, 3u + f);
            stark_lab_autobaud_t r;
            char msg[40];
            snprintf(msg, sizeof msg, "%u %s", (unsigned)baud, stark_lab_uart_fmt_name(f));
            TEST_ASSERT_EQUAL_MESSAGE(STARK_OK, lab_uartsvc_autobaud(&s_ses, &r), msg);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(STARK_LAB_BAUD_OK, (unsigned)r.status, msg);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(baud, r.baud, msg);
            char want[16];
            snprintf(want, sizeof want, "%u", (unsigned)baud);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(want, last_text(LAB_UART_LOG_AUTOBAUD), msg);
            /* the RX pin only, and only for the capture; both free after */
            TEST_ASSERT_FALSE_MESSAGE(t.held_tx_in_edges, msg);
            TEST_ASSERT_TRUE_MESSAGE(t.held_rx_in_edges, msg);
            TEST_ASSERT_FALSE_MESSAGE(held(39), msg);
            TEST_ASSERT_EQUAL_MESSAGE(FAKE_LAB_PAD_HIZ, fake_lab_mode(39), msg);
        }
    }
}

static void test_autobaud_says_unknown_when_nothing_fits(void)
{
    stark_lab_autobaud_t r;
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 14400, F8N1); /* between two listed bauds */
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_autobaud(&s_ses, &r));
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_BAUD_UNKNOWN, (unsigned)r.status);
    TEST_ASSERT_EQUAL_UINT32(0, r.baud);
    TEST_ASSERT_EQUAL_STRING("unknown", last_text(LAB_UART_LOG_AUTOBAUD));
    fake_lab_uart_peer(FAKE_LAB_UART_SILENT, 0, 0); /* a line that says nothing */
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_autobaud(&s_ses, &r));
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_BAUD_TOO_FEW, (unsigned)r.status);
    fake_lab_uart_peer(FAKE_LAB_UART_ECHO, 38400, F8N1); /* an echo has nothing to echo */
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_autobaud(&s_ses, &r));
    TEST_ASSERT_EQUAL_UINT(STARK_LAB_BAUD_TOO_FEW, (unsigned)r.status);
}

static void test_autobaud_refuses_what_it_cannot_do_and_gives_the_pin_back(void)
{
    stark_lab_autobaud_t r;
    fake_lab_uart_pins(38, 39);
    fake_lab_uart_peer(FAKE_LAB_UART_TEXT, 38400, F8N1);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_uartsvc_autobaud(&s_ses, NULL));
    /* the driver owns the pin while a session is open */
    open_ok(B115200, F8N1);
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_autobaud(&s_ses, &r));
    TEST_ASSERT_EQUAL_UINT(0, t.edge_calls);
    lab_uartsvc_close(&s_ses);
    /* the GPIO app has the pin */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_pins, 39, "gpio"));
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, lab_uartsvc_autobaud(&s_ses, &r));
    TEST_ASSERT_TRUE(held(39)); /* still the GPIO app's */
    lab_pins_release(&s_pins, 39);
    /* asked to stop */
    t.cancelled = true;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_autobaud(&s_ses, &r));
    t.cancelled = false;
    t.cancel_edges = true; /* the capture was cut short */
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_uartsvc_autobaud(&s_ses, &r));
    TEST_ASSERT_FALSE(held(39));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(39));
    t.cancel_edges = false;
    t.fail_edges = true;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_uartsvc_autobaud(&s_ses, &r));
    TEST_ASSERT_FALSE(held(39));
    t.fail_edges = false;
    t.fail_pad_in = true;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_uartsvc_autobaud(&s_ses, &r));
    TEST_ASSERT_FALSE(held(39));
    t.fail_pad_in = false;
    TEST_ASSERT_EQUAL(STARK_OK, lab_uartsvc_autobaud(&s_ses, &r)); /* and it works again */
    TEST_ASSERT_EQUAL_UINT32(38400, r.baud);
}

/* ---- the transport's guard --------------------------------------------------------------------
 */

static void test_the_shim_refuses_what_the_hardware_transport_refuses(void)
{
    uint8_t buf[4] = {0};
    stark_lab_uart_err_t e;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_uart_open(1199, 0, 2048, false));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_uart_open(921601, 0, 2048, false));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_uart_open(115200, 5, 2048, false));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_uart_open(115200, 0, 255, false));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_uart_open(115200, 0, 8193, false));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, stark_lab_transport_uart_set_baud(9600));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_uart_set_baud(0));
    TEST_ASSERT_EQUAL(-(int)STARK_ERR_STATE, stark_lab_transport_uart_read(buf, 4, 0, &e));
    TEST_ASSERT_EQUAL(-(int)STARK_ERR_STATE, stark_lab_transport_uart_write(buf, 4, 0));
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_uart_open(115200, 0, 2048, false));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, stark_lab_transport_uart_open(115200, 0, 2048, false));
    TEST_ASSERT_EQUAL(-(int)STARK_ERR_INVALID_ARG, stark_lab_transport_uart_read(NULL, 4, 0, &e));
    TEST_ASSERT_EQUAL(-(int)STARK_ERR_INVALID_ARG, stark_lab_transport_uart_read(buf, 0, 0, NULL));
    TEST_ASSERT_EQUAL(-(int)STARK_ERR_INVALID_ARG, stark_lab_transport_uart_read(buf, 257, 0, &e));
    TEST_ASSERT_EQUAL(-(int)STARK_ERR_INVALID_ARG, stark_lab_transport_uart_read(buf, 4, 5001, &e));
    TEST_ASSERT_EQUAL(0, stark_lab_transport_uart_read(buf, 4, 0, NULL));
    TEST_ASSERT_EQUAL(-(int)STARK_ERR_INVALID_ARG, stark_lab_transport_uart_write(NULL, 4, 0));
    TEST_ASSERT_EQUAL(-(int)STARK_ERR_INVALID_ARG, stark_lab_transport_uart_write(buf, 0, 0));
    TEST_ASSERT_EQUAL(-(int)STARK_ERR_INVALID_ARG, stark_lab_transport_uart_write(buf, 65, 0));
    TEST_ASSERT_EQUAL(-(int)STARK_ERR_INVALID_ARG, stark_lab_transport_uart_write(buf, 4, 5001));
    TEST_ASSERT_EQUAL(-(int)STARK_ERR_STATE,
                      stark_lab_transport_uart_write(buf, 4, 100)); /* no TX */
    stark_lab_transport_uart_close();
    /* a write that cannot finish in its time says so */
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_uart_open(1200, 0, 2048, true));
    TEST_ASSERT_EQUAL(-(int)STARK_ERR_TIMEOUT, stark_lab_transport_uart_write(buf, 4, 10));
    stark_lab_transport_uart_close();
    stark_lab_transport_uart_close();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_holds_both_pins_and_closing_gives_everything_back);
    RUN_TEST(test_open_refuses_what_it_cannot_do_and_holds_nothing);
    RUN_TEST(test_nothing_is_sent_until_tx_is_armed);
    RUN_TEST(test_a_refused_arm_or_a_failed_reinstall);
    RUN_TEST(test_a_new_baud_or_format_clears_the_ring_and_keeps_tx);
    RUN_TEST(test_a_failed_change_ends_the_session);
    RUN_TEST(test_poll_fills_the_ring_with_what_the_peer_said);
    RUN_TEST(test_a_full_ring_counts_what_it_overwrites);
    RUN_TEST(test_errors_are_counted_and_announced);
    RUN_TEST(test_a_buffer_that_overflows_is_an_overrun);
    RUN_TEST(test_a_failed_read_ends_the_session);
    RUN_TEST(test_clear_empties_the_ring_the_counters_and_the_driver);
    RUN_TEST(test_send_goes_out_once_and_only_within_its_bounds);
    RUN_TEST(test_a_slow_line_still_sends_32_bytes);
    RUN_TEST(test_a_failed_write_ends_the_session);
    RUN_TEST(test_the_loopback_round_trips_at_every_baud_and_format);
    RUN_TEST(test_a_peer_that_echoes_correctly_passes);
    RUN_TEST(test_a_mismatched_peer_is_reported_with_its_errors);
    RUN_TEST(test_a_mute_peer_is_no_data_after_the_whole_window);
    RUN_TEST(test_bytes_left_over_from_a_send_are_flushed_first);
    RUN_TEST(test_the_loopback_can_be_cancelled_and_never_hangs);
    RUN_TEST(test_a_failing_driver_ends_a_loopback);
    RUN_TEST(test_a_drained_stream_does_not_keep_the_flush_going_forever);
    RUN_TEST(test_autobaud_names_the_peer_at_every_baud_and_format);
    RUN_TEST(test_autobaud_says_unknown_when_nothing_fits);
    RUN_TEST(test_autobaud_refuses_what_it_cannot_do_and_gives_the_pin_back);
    RUN_TEST(test_the_shim_refuses_what_the_hardware_transport_refuses);
    return UNITY_END();
}
