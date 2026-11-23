/*
 * test_subghz_tx.c — the Sub-GHz service's gated replay (TASKS.md STARK-0605, ADR-0022 §6) over
 * the real module core, a fake 24C02 and the fake CC1101 recording what GDO0 carries. A request
 * sends nothing; one accepted request sends exactly the file, once, at PATABLE 0x60 inside the
 * band, and leaves the radio idle; every refusal sends nothing and says why; a transmission
 * that fails at any transfer, on a dead bus, on a stuck chip or on a transport timeout is
 * bounded, counted against the budget, never repeated and never leaves the radio out of IDLE;
 * the band and the power table are read back from the chip before STX; the duty window
 * outlives a lost module and a wrapping clock; and a sender that asks as fast as it may never
 * gets more than 10 % on air or less than nine times each transmission's length between two.
 */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "fake_24c02.h"
#include "fake_cc1101.h"
#include "fake_module_catalog.h"
#include "fake_subghz_scenes.h"
#include "module_core.h"
#include "subghz_svc.h"
#include "unity.h"

#define A0 0x50

static module_core_t s_mc;
static subghz_svc_t s_svc;
static uint32_t s_now;
static bool s_open;
static stark_module_lease_t s_open_lease;
static unsigned s_rx_starts, s_rx_stops;
static bool s_rx_on;
static unsigned s_xfer_n;      /* transfers since the test armed the counter */
static unsigned s_fail_at;     /* the n-th transfer fails, once (0: none) */
static unsigned s_fail_from;   /* every transfer from the n-th on fails (0: none) */
static void (*s_before)(void); /* run before the nth transfer that starts with s_before_hdr */
static uint8_t s_before_hdr;
static unsigned s_before_nth, s_before_seen;
static uint8_t s_flip_hdr; /* the nth read that starts with this has a data bit flipped */
static unsigned s_flip_nth, s_flip_seen;
static unsigned s_tx_calls;
static stark_err_t s_tx_err; /* what the transport answers (a hang: the whole timeout passes) */
static uint32_t s_tx_timeout;
static size_t s_tx_n;
static uint8_t s_tx_marc, s_tx_iocfg0;
static uint32_t s_tx_start_ms;
static int s_evts[256];
static size_t s_nevts;

typedef struct {
    subghz_tx_verdict_t verdict;
    stark_err_t err;
    char name[16];
    uint32_t hz, symbols, ms;
} txlog_t;
static txlog_t s_txlog[64];
static size_t s_ntx;

typedef struct {
    subghz_log_t what;
    unsigned a, b;
} logged_t;
static logged_t s_log[64];
static size_t s_nlog;

static subghz_sym_t s_sym[SUBGHZ_RAW_MAX_SYMBOLS + 8];
static subghz_raw_t s_raw;
static subghz_tx_info_t s_info;
static subghz_sym_t s_buf[SUBGHZ_RAW_MAX_SYMBOLS + 4];

static stark_err_t mc_probe(void *ctx, uint8_t addr)
{
    (void)ctx;
    return fake_24c02_probe(addr);
}

static stark_err_t mc_read(void *ctx, uint8_t addr, uint8_t off, uint8_t *buf, size_t len)
{
    (void)ctx;
    return fake_24c02_read_at(addr, off, buf, len);
}

static const module_io_t k_mio = {mc_probe, mc_read, NULL, NULL, NULL};
static const module_wiring_t k_board[] = {{A0, 40, 41, 42}, {0x51, -1, -1, -1}};

static size_t io_list(void *ctx, stark_module_entry_t *out, size_t max)
{
    (void)ctx;
    return module_core_list(&s_mc, out, max);
}

static stark_err_t io_claim(void *ctx, uint8_t slot, stark_module_lease_t *out)
{
    (void)ctx;
    return module_core_claim(&s_mc, slot, STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ, "subghz", out);
}

static void io_release(void *ctx, const stark_module_lease_t *l)
{
    (void)ctx;
    module_core_release(&s_mc, l);
}

static bool io_lease_valid(void *ctx, const stark_module_lease_t *l)
{
    (void)ctx;
    return module_core_lease_valid(&s_mc, l);
}

static stark_err_t io_open(void *ctx, const stark_module_lease_t *l)
{
    (void)ctx;
    s_open_lease = *l;
    s_open = true;
    return STARK_OK;
}

static void io_close(void *ctx)
{
    (void)ctx;
    s_open = false;
}

static stark_err_t io_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    (void)ctx;
    if (!s_open || !module_core_lease_valid(&s_mc, &s_open_lease)) {
        return STARK_ERR_STATE;
    }
    unsigned n = ++s_xfer_n;
    if ((s_fail_from != 0 && n >= s_fail_from) || (s_fail_at != 0 && n == s_fail_at)) {
        return STARK_ERR_IO;
    }
    if (s_before != NULL && tx[0] == s_before_hdr && ++s_before_seen == s_before_nth) {
        s_before(); /* the chip is changed behind the driver's back, just before this transfer */
    }
    stark_err_t err = fake_cc1101_xfer(tx, rx, len);
    if (s_flip_hdr != 0 && tx[0] == s_flip_hdr && ++s_flip_seen == s_flip_nth && len > 1) {
        rx[1] ^= 1u;
    }
    return err;
}

static stark_err_t io_rx_start(void *ctx)
{
    (void)ctx;
    s_rx_starts++;
    s_rx_on = true;
    return STARK_OK;
}

static size_t io_rx_take(void *ctx, subghz_sym_t *out, size_t max, uint32_t timeout_ms)
{
    (void)ctx;
    size_t n = fake_cc1101_gdo2_take(out, max);
    if (n == 0) {
        s_now += timeout_ms;
    }
    return n;
}

static void io_rx_stop(void *ctx)
{
    (void)ctx;
    s_rx_stops++;
    s_rx_on = false;
}

static uint32_t io_now(void *ctx)
{
    (void)ctx;
    return s_now;
}

static void io_delay(void *ctx, uint32_t ms)
{
    (void)ctx;
    s_now += ms;
}

static void io_log(void *ctx, subghz_log_t what, unsigned a, unsigned b)
{
    (void)ctx;
    if (s_nlog < sizeof s_log / sizeof s_log[0]) {
        s_log[s_nlog++] = (logged_t){what, a, b};
    }
}

static void io_changed(void *ctx, bool ready)
{
    (void)ctx;
    module_core_confirm(&s_mc, STARK_CAP_SUBGHZ, ready);
}

static void io_notify(void *ctx, int evt)
{
    (void)ctx;
    if (s_nevts < sizeof s_evts / sizeof s_evts[0]) {
        s_evts[s_nevts++] = evt;
    }
}

static stark_err_t io_tx(void *ctx, const subghz_sym_t *s, size_t n, uint32_t timeout_ms)
{
    (void)ctx;
    s_tx_calls++;
    s_tx_timeout = timeout_ms;
    s_tx_n = n;
    s_tx_start_ms = s_now;
    TEST_ASSERT_TRUE_MESSAGE(stark_subghz_tx_ok(s, n, timeout_ms),
                             "the transport would refuse what the service sent");
    TEST_ASSERT_FALSE_MESSAGE(s_rx_on, "the receiver is off while transmitting");
    s_tx_marc = fake_cc1101_stats()->marcstate;
    s_tx_iocfg0 = fake_cc1101_stats()->reg[CC1101_IOCFG0];
    if (s_tx_err != STARK_OK) {
        s_now += timeout_ms; /* a hang costs the whole timeout, not more */
        return s_tx_err;
    }
    if (!fake_cc1101_gdo0_put(s, n)) {
        return STARK_ERR_STATE;
    }
    uint32_t us = 0;
    for (size_t i = 0; i < n; i++) {
        us += (uint32_t)(s[i] < 0 ? -s[i] : s[i]);
    }
    s_now += (us + 999u) / 1000u; /* the time on air */
    return STARK_OK;
}

static void io_log_tx(void *ctx, const subghz_tx_log_t *w)
{
    (void)ctx;
    if (s_ntx < sizeof s_txlog / sizeof s_txlog[0]) {
        txlog_t *t = &s_txlog[s_ntx++];
        t->verdict = w->verdict;
        t->err = w->err;
        snprintf(t->name, sizeof t->name, "%s", w->name);
        t->hz = w->freq_hz;
        t->symbols = w->symbols;
        t->ms = w->ms;
    }
}

static const subghz_svc_io_t k_io = {io_list,  io_claim,  io_release,  io_lease_valid, io_open,
                                     io_close, io_xfer,   io_rx_start, io_rx_take,     io_rx_stop,
                                     io_now,   io_delay,  io_log,      io_changed,     io_notify,
                                     io_tx,    io_log_tx, NULL};

static void start_at(uint32_t t0)
{
    fake_24c02_reset();
    fake_cc1101_reset();
    module_core_init(&s_mc, &k_mio, k_board, 2);
    s_now = t0; /* the service starts here: the boot lock runs from this moment */
    subghz_svc_init(&s_svc, &k_io);
    s_open = false;
    s_rx_starts = s_rx_stops = 0;
    s_rx_on = false;
    s_xfer_n = s_fail_at = s_fail_from = 0;
    s_before = NULL;
    s_before_seen = 0;
    s_flip_hdr = 0;
    s_flip_seen = 0;
    s_tx_calls = 0;
    s_tx_err = STARK_OK;
    s_tx_timeout = 0;
    s_nevts = 0;
    s_nlog = 0;
    s_ntx = 0;
}

void setUp(void)
{
    start_at(10000);
}
void tearDown(void)
{}

static void module_in(void)
{
    fake_24c02_put(A0, k_fake_modules[FAKE_MODULE_CC1101_MODULE - 1].image, MODULE_DESC_SIZE);
    for (int i = 0; i < 2; i++) {
        (void)module_core_tick(&s_mc);
    }
}

static void ready(void)
{
    module_in();
    subghz_svc_step(&s_svc);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    s_nlog = 0;
    s_nevts = 0;
}

static void run_for(uint32_t ms)
{
    uint32_t end = s_now + ms;
    while ((int32_t)(end - s_now) > 0) {
        uint32_t wait = subghz_svc_step(&s_svc);
        TEST_ASSERT_TRUE(wait >= 1 && wait <= SUBGHZ_IDLE_MS);
        s_now += wait;
    }
}

/* Ready, and the boot lock has run out. */
static void ready_to_send(void)
{
    ready();
    run_for(SUBGHZ_TX_BOOT_LOCK_MS + 300);
}

static size_t count_events(int evt)
{
    size_t n = 0;
    for (size_t i = 0; i < s_nevts; i++) {
        n += s_evts[i] == evt ? 1u : 0u;
    }
    return n;
}

static size_t count_tx_logs(subghz_tx_verdict_t v)
{
    size_t n = 0;
    for (size_t i = 0; i < s_ntx; i++) {
        n += s_txlog[i].verdict == v ? 1u : 0u;
    }
    return n;
}

static void load_syms(const subghz_sym_t *v, size_t n)
{
    memcpy(s_sym, v, n * sizeof s_sym[0]);
    s_raw = (subghz_raw_t){433920000u, SUBGHZ_MOD_OOK, 4800u, s_sym, (uint16_t)n};
}

static void load_scene(int id)
{
    const fake_subghz_scene_t *sc = &k_fake_scenes[id - 1];
    load_syms(sc->sym, sc->n);
}

static uint32_t hash(void)
{
    return subghz_raw_hash(s_raw.sym, s_raw.n);
}

static subghz_tx_verdict_t send(void)
{
    return subghz_svc_tx(&s_svc, &s_raw, "rf_0001.rf", hash(), &s_info);
}

static subghz_tx_verdict_t check(void)
{
    return subghz_svc_tx_check(&s_svc, &s_raw, "rf_0001.rf", &s_info);
}

/* One round: the step that carries a pending request out. */
static void go(void)
{
    run_for(30);
}

static void assert_nothing_sent(void)
{
    const fake_cc1101_stats_t *st = fake_cc1101_stats();
    TEST_ASSERT_EQUAL_MESSAGE(0, st->tx_strobes, "STX was sent");
    TEST_ASSERT_EQUAL_MESSAGE(0, st->gdo0_symbols, "a symbol reached GDO0");
    TEST_ASSERT_EQUAL(0, st->gdo0_refused);
    TEST_ASSERT_EQUAL(0, s_tx_calls);
}

static void assert_power_and_band_held(void)
{
    const fake_cc1101_stats_t *st = fake_cc1101_stats();
    TEST_ASSERT_TRUE(st->patable_max <= 0x60);
    TEST_ASSERT_EQUAL_MESSAGE(0, st->patable_other, "only 0x00 and 0x60 are ever written");
    TEST_ASSERT_EQUAL_MESSAGE(0, st->out_of_band, "never tuned outside 433.05-434.79 MHz");
}

static void assert_idle(void)
{
    TEST_ASSERT_EQUAL_HEX8(CC1101_MARC_IDLE, fake_cc1101_stats()->marcstate);
    TEST_ASSERT_FALSE(s_rx_on);
    TEST_ASSERT_EQUAL(s_rx_starts, s_rx_stops);
}

/* ---- one request, one transmission ------------------------------------------------------ */

static void test_a_request_sends_the_file_once_exactly_and_idles_the_radio(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check());
    TEST_ASSERT_EQUAL_UINT32(202, s_info.duration_ms);
    TEST_ASSERT_EQUAL_UINT32(360000, s_info.budget_left_ms);
    TEST_ASSERT_EQUAL_UINT32(0, s_info.wait_ms);
    TEST_ASSERT_EQUAL_HEX32(hash(), s_info.hash);
    TEST_ASSERT_EQUAL_HEX32(k_fake_scenes[0].kept_hash,
                            s_info.hash); /* the generator's, not ours */
    assert_nothing_sent();                /* asking what it would be sends nothing */
    TEST_ASSERT_EQUAL(0, s_ntx);

    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    assert_nothing_sent(); /* a request alone sends nothing: the service task does, next round */
    stark_subghz_status_t st;
    subghz_svc_status(&s_svc, &st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_TX, st.mode);
    TEST_ASSERT_FALSE(st.tx_done);

    go();
    const fake_cc1101_stats_t *f = fake_cc1101_stats();
    TEST_ASSERT_EQUAL(1, s_tx_calls);
    TEST_ASSERT_EQUAL(1, f->tx_strobes);
    TEST_ASSERT_EQUAL(192, f->gdo0_symbols);
    TEST_ASSERT_EQUAL(0, f->gdo0_refused);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(CC1101_MARC_TX, s_tx_marc, "in TX while the symbols went out");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x2D, s_tx_iocfg0, "GDO0 is the serial data input");
    TEST_ASSERT_EQUAL_UINT32(202u + STARK_SUBGHZ_TX_MARGIN_MS, s_tx_timeout);
    const subghz_sym_t *rec;
    size_t n = fake_cc1101_gdo0_recorded(&rec);
    TEST_ASSERT_EQUAL(192, n);
    TEST_ASSERT_EQUAL_INT32_ARRAY(k_fake_scenes[0].sym, rec, 192); /* GDO0 carried the file */
    TEST_ASSERT_EQUAL_HEX32(k_fake_scenes[0].kept_hash, subghz_raw_hash(rec, n));
    assert_idle();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x2E, f->reg[CC1101_IOCFG0], "the receive profile is back");
    assert_power_and_band_held();
    TEST_ASSERT_EQUAL_HEX8(0x60, f->patable[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, f->patable[0]);

    TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_TX_DONE));
    TEST_ASSERT_EQUAL(0, count_events(STARK_SUBGHZ_EVT_TX_FAILED));
    TEST_ASSERT_EQUAL(0, count_events(STARK_SUBGHZ_EVT_TX_REFUSED));
    TEST_ASSERT_EQUAL(1, s_ntx);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, s_txlog[0].verdict);
    TEST_ASSERT_EQUAL(STARK_OK, s_txlog[0].err);
    TEST_ASSERT_EQUAL_STRING("rf_0001.rf", s_txlog[0].name);
    TEST_ASSERT_EQUAL_UINT32(433920000u, s_txlog[0].hz);
    TEST_ASSERT_EQUAL_UINT32(192, s_txlog[0].symbols);
    TEST_ASSERT_EQUAL_UINT32(202, s_txlog[0].ms);
    subghz_svc_status(&s_svc, &st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_TX, st.mode);
    TEST_ASSERT_TRUE(st.tx_done);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, st.tx_verdict);
    TEST_ASSERT_EQUAL(STARK_OK, st.tx_err);

    /* nothing sends it again, however long the service runs */
    run_for(60000);
    TEST_ASSERT_EQUAL(1, s_tx_calls);
    TEST_ASSERT_EQUAL(1, fake_cc1101_stats()->tx_strobes);
    TEST_ASSERT_EQUAL(192, fake_cc1101_stats()->gdo0_symbols);
    assert_idle();
    /* and it is charged: 202 ms of the 360 s, nine times that to wait */
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check());
    TEST_ASSERT_EQUAL_UINT32(360000u - 202u, s_info.budget_left_ms);
}

static void test_an_immediate_second_request_is_refused_for_the_off_time(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    go();
    TEST_ASSERT_EQUAL(1, s_tx_calls);
    s_ntx = 0;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, send());
    TEST_ASSERT_TRUE(s_info.wait_ms > 0 && s_info.wait_ms <= 9u * 202u);
    TEST_ASSERT_EQUAL(1, s_ntx);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, s_txlog[0].verdict);
    run_for(9u * 202u + 300u);
    TEST_ASSERT_EQUAL(1, s_tx_calls);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    go();
    TEST_ASSERT_EQUAL(2, s_tx_calls);
    TEST_ASSERT_EQUAL(384, fake_cc1101_stats()->gdo0_symbols);
}

static void test_the_fake_own_receiver_hears_the_replay_back_exactly(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    fake_cc1101_own_rx(true);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    go();
    TEST_ASSERT_EQUAL(1, s_tx_calls);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf,
                                                   SUBGHZ_RAW_MAX_SYMBOLS));
    run_for(500);
    subghz_svc_capture_finish(&s_svc);
    run_for(100);
    stark_subghz_status_t st;
    subghz_svc_status(&s_svc, &st);
    TEST_ASSERT_TRUE(st.capture_done);
    TEST_ASSERT_EQUAL(192, st.symbols);
    TEST_ASSERT_EQUAL_INT32_ARRAY(k_fake_scenes[0].sym, s_buf, 192);
    TEST_ASSERT_EQUAL_HEX32(k_fake_scenes[0].kept_hash, subghz_raw_hash(s_buf, st.symbols));
    TEST_ASSERT_EQUAL_UINT32(201600, st.span_us);
    assert_idle();
}

static void test_the_largest_transmissions_and_their_timeouts(void)
{
    ready_to_send();
    /* 2048 symbols of 100 us */
    subghz_sym_t dense[2048];
    for (size_t i = 0; i < 2048; i++) {
        dense[i] = (i & 1u) ? -100 : 100;
    }
    load_syms(dense, 2048);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    go();
    TEST_ASSERT_EQUAL(2048, s_tx_n);
    TEST_ASSERT_EQUAL_UINT32(205u + STARK_SUBGHZ_TX_MARGIN_MS, s_tx_timeout);
    TEST_ASSERT_EQUAL(2048, fake_cc1101_stats()->gdo0_symbols);
    const subghz_sym_t *rec;
    TEST_ASSERT_EQUAL(2048, fake_cc1101_gdo0_recorded(&rec));
    TEST_ASSERT_EQUAL_INT32_ARRAY(dense, rec, 2048);
    run_for(9u * 205u + 300u);
    /* exactly 2 s of signal */
    const subghz_sym_t two_s[2] = {1000000, -1000000};
    load_syms(two_s, 2);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    go();
    TEST_ASSERT_EQUAL(2, s_tx_n);
    TEST_ASSERT_EQUAL_UINT32(2000u + STARK_SUBGHZ_TX_MARGIN_MS, s_tx_timeout);
    TEST_ASSERT_EQUAL(2, s_tx_calls);
    TEST_ASSERT_EQUAL_UINT32(360000u - 205u - 2000u,
                             subghz_txpolicy_budget_left_ms(&s_svc.pol, s_now));
    assert_idle();
    assert_power_and_band_held();
}

/* ---- nothing transmits unless it was asked to ----------------------------------------------- */

static void test_no_measurement_and_no_idling_ever_transmits(void)
{
    ready();
    fake_cc1101_air(433920000u, -40);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    run_for(500);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    run_for(2000);
    fake_cc1101_scene(k_fake_scenes[0].sym, k_fake_scenes[0].n);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf,
                                                   SUBGHZ_RAW_MAX_SYMBOLS));
    run_for(500);
    subghz_svc_capture_finish(&s_svc);
    run_for(200);
    subghz_svc_stop(&s_svc);
    run_for(SUBGHZ_TX_BOOT_LOCK_MS + 5000);
    assert_nothing_sent();
    assert_power_and_band_held();
    TEST_ASSERT_EQUAL(0, s_ntx);
    TEST_ASSERT_EQUAL(0, count_events(STARK_SUBGHZ_EVT_TX_DONE));
}

/* ---- every refusal sends nothing and says why ------------------------------------------------ */

static void expect_refused(subghz_tx_verdict_t want)
{
    size_t logs = s_ntx;
    TEST_ASSERT_EQUAL_MESSAGE(want, check(), "check");
    TEST_ASSERT_EQUAL(logs + 1u, s_ntx);
    TEST_ASSERT_EQUAL(want, s_txlog[s_ntx - 1u].verdict);
    TEST_ASSERT_EQUAL_STRING("rf_0001.rf", s_txlog[s_ntx - 1u].name);
    logs = s_ntx;
    TEST_ASSERT_EQUAL_MESSAGE(want, send(), "send");
    TEST_ASSERT_EQUAL(logs + 1u, s_ntx);
    assert_nothing_sent();
    run_for(500); /* and nothing is left pending that a later round could send */
    assert_nothing_sent();
    stark_subghz_status_t st;
    subghz_svc_status(&s_svc, &st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_IDLE, st.mode);
}

static void test_refusal_no_module_and_the_boot_lock(void)
{
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    expect_refused(SUBGHZ_TX_REFUSE_NO_MODULE); /* nothing declares a radio */
    ready();
    expect_refused(SUBGHZ_TX_REFUSE_OFFTIME); /* just booted: the longest off-time is running */
    TEST_ASSERT_TRUE(s_info.wait_ms > 0 && s_info.wait_ms <= SUBGHZ_TX_BOOT_LOCK_MS);
    run_for(SUBGHZ_TX_BOOT_LOCK_MS - 3000); /* the lock runs from the service's start, 10 s mark */
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, check());
    run_for(5000);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check());
}

static void test_refusal_band(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    static const uint32_t bad[] = {433049999u, 434790001u, 0u, 315000000u, 868000000u, UINT32_MAX};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        s_raw.freq_hz = bad[i];
        expect_refused(SUBGHZ_TX_REFUSE_BAND);
    }
    s_raw.freq_hz = 433050000u;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check());
    s_raw.freq_hz = 434790000u;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check());
}

static void test_refusal_length(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    s_raw.n = 0;
    expect_refused(SUBGHZ_TX_REFUSE_LENGTH);
    for (size_t i = 0; i < SUBGHZ_RAW_MAX_SYMBOLS + 8; i++) {
        s_sym[i] = (i & 1u) ? -100 : 100;
    }
    s_raw.n = 2049;
    expect_refused(SUBGHZ_TX_REFUSE_LENGTH);
    s_raw.n = 2048;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check());
    /* a count far past the buffer is refused without reading a symbol (ASan would see it) */
    subghz_sym_t tiny[4] = {100, -100, 100, -100};
    s_raw = (subghz_raw_t){433920000u, SUBGHZ_MOD_OOK, 4800u, tiny, UINT16_MAX};
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_LENGTH, subghz_svc_tx_check(&s_svc, &s_raw, "x", &s_info));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_LENGTH, subghz_svc_tx(&s_svc, &s_raw, "x", 0, NULL));
    assert_nothing_sent();
}

static void test_refusal_duration(void)
{
    ready_to_send();
    const subghz_sym_t over[3] = {1000000, -1000000, 1}; /* 2 000 001 us */
    load_syms(over, 3);
    expect_refused(SUBGHZ_TX_REFUSE_DURATION);
    const subghz_sym_t three_s[3] = {1000000, -1000000, 1000000};
    load_syms(three_s, 3);
    expect_refused(SUBGHZ_TX_REFUSE_DURATION);
    const subghz_sym_t exact[3] = {1000000, -999999, 1};
    load_syms(exact, 3);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check());
    /* a span that would wrap a 32-bit sum is clamped, not wrapped to something small */
    subghz_sym_t huge[8];
    for (size_t i = 0; i < 8; i++) {
        huge[i] = (i & 1u) ? INT32_MIN : INT32_MAX;
    }
    load_syms(huge, 8);
    expect_refused(SUBGHZ_TX_REFUSE_DURATION);
}

static void test_refusal_budget(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    for (int i = 0; i < 180; i++) { /* 360 s sent, the last of it 28 s ago */
        subghz_txpolicy_record(&s_svc.pol, s_now - 30000u, s_now - 28000u, 2000);
    }
    expect_refused(SUBGHZ_TX_REFUSE_BUDGET);
    TEST_ASSERT_EQUAL_UINT32(0, s_info.budget_left_ms);
}

static void test_refusal_file(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    s_raw.mod = SUBGHZ_MOD_2FSK; /* an OOK profile is the only one there is to send with */
    expect_refused(SUBGHZ_TX_REFUSE_FILE);
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    s_raw.rate = 599;
    expect_refused(SUBGHZ_TX_REFUSE_FILE);
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    s_sym[1] = 400; /* signs no longer alternate */
    expect_refused(SUBGHZ_TX_REFUSE_FILE);
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    s_sym[0] = -s_sym[0]; /* a low first */
    expect_refused(SUBGHZ_TX_REFUSE_FILE);
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    s_sym[5] = 0;
    expect_refused(SUBGHZ_TX_REFUSE_FILE);
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    s_sym[8] = 1000001; /* a symbol past 1 s */
    expect_refused(SUBGHZ_TX_REFUSE_FILE);
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    s_raw.sym = NULL; /* no symbols to hash either */
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_FILE, subghz_svc_tx_check(&s_svc, &s_raw, "x", &s_info));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_FILE, subghz_svc_tx(&s_svc, &s_raw, "x", 0, NULL));
    assert_nothing_sent();
    /* no description at all */
    size_t logs = s_ntx;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_FILE, subghz_svc_tx_check(&s_svc, NULL, "x", &s_info));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_FILE, subghz_svc_tx(&s_svc, NULL, "x", 0, NULL));
    TEST_ASSERT_EQUAL(logs + 2u, s_ntx);
    assert_nothing_sent();
}

static void test_refusal_a_file_that_is_not_what_was_confirmed(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check());
    uint32_t shown = s_info.hash;
    s_sym[10] = 410; /* the symbols changed after the confirmation was shown */
    size_t logs = s_ntx;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_FILE,
                      subghz_svc_tx(&s_svc, &s_raw, "rf_0001.rf", shown, NULL));
    TEST_ASSERT_EQUAL(logs + 1u, s_ntx);
    assert_nothing_sent();
    run_for(500);
    assert_nothing_sent();
    s_sym[10] = k_fake_scenes[0].sym[10]; /* put back: the same hash is accepted */
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, subghz_svc_tx(&s_svc, &s_raw, "rf_0001.rf", shown, NULL));
}

static void test_refusal_busy_one_request_at_a_time(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    size_t logs = s_ntx;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BUSY, send());
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BUSY, check());
    TEST_ASSERT_EQUAL(logs + 2u, s_ntx);
    TEST_ASSERT_EQUAL_STRING("rf_0001.rf", s_txlog[s_ntx - 1u].name);
    go();
    TEST_ASSERT_EQUAL(1, s_tx_calls); /* two more requests did not make a second transmission */
    TEST_ASSERT_EQUAL(1, fake_cc1101_stats()->tx_strobes);
}

/* ---- the transmit path checks again ---------------------------------------------------------- */

static void test_the_transmit_path_checks_everything_again(void)
{
    /* the symbols change between the request and the round that sends it */
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    s_sym[20] = 420;
    go();
    assert_nothing_sent();
    TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_TX_REFUSED));
    TEST_ASSERT_EQUAL(0, count_events(STARK_SUBGHZ_EVT_TX_DONE));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_FILE, s_txlog[s_ntx - 1u].verdict);
    stark_subghz_status_t st;
    subghz_svc_status(&s_svc, &st);
    TEST_ASSERT_TRUE(st.tx_done);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_FILE, st.tx_verdict);
    assert_idle();
    /* a refusal on the transmit path is not a transmission: nothing charged, no off-time */
    s_sym[20] = k_fake_scenes[0].sym[20];
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    go();
    TEST_ASSERT_EQUAL(1, s_tx_calls);

    /* the budget is used up behind the request's back */
    start_at(10000);
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    for (int i = 0; i < 180; i++) {
        subghz_txpolicy_record(&s_svc.pol, s_now - 30000u, s_now - 28000u, 2000);
    }
    go();
    assert_nothing_sent();
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_BUDGET, s_txlog[s_ntx - 1u].verdict);
    TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_TX_REFUSED));

    /* an off-time starts behind the request's back */
    start_at(10000);
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    subghz_txpolicy_record(&s_svc.pol, s_now, s_now + 100, 100);
    go();
    assert_nothing_sent();
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, s_txlog[s_ntx - 1u].verdict);

    /* the request was copied: a description edited afterwards changes nothing it sends */
    start_at(10000);
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    s_raw.freq_hz = 868000000u;
    s_raw.n = 4;
    go();
    TEST_ASSERT_EQUAL(1, s_tx_calls);
    TEST_ASSERT_EQUAL(192, s_tx_n);
    TEST_ASSERT_TRUE(fake_cc1101_stats()->tuned_hz > 433900000u &&
                     fake_cc1101_stats()->tuned_hz < 433940000u); /* never retuned to the edit */
    assert_power_and_band_held();
}

static void test_stop_and_any_other_request_drop_a_pending_send(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    subghz_svc_stop(&s_svc);
    run_for(2000);
    assert_nothing_sent();
    assert_idle();
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send()); /* not BUSY: the old request is gone */
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    run_for(500);
    assert_nothing_sent();
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    run_for(2000);
    assert_nothing_sent();
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 64));
    run_for(500);
    assert_nothing_sent();
    subghz_svc_stop(&s_svc);
    run_for(500);
    assert_idle();
    TEST_ASSERT_EQUAL(0, s_ntx); /* a dropped request is neither a transmission nor a refusal */
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    go();
    TEST_ASSERT_EQUAL(1, s_tx_calls); /* and a fresh one still works */
}

static void test_a_send_during_a_capture_stops_the_receiver_first(void)
{
    ready_to_send();
    fake_cc1101_scene(k_fake_scenes[0].sym, k_fake_scenes[0].n);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf,
                                                   SUBGHZ_RAW_MAX_SYMBOLS));
    run_for(300);
    TEST_ASSERT_TRUE(s_rx_on);
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    go(); /* io_tx asserts the receiver is off */
    TEST_ASSERT_EQUAL(1, s_tx_calls);
    assert_idle();
}

/* ---- a transmission that fails ----------------------------------------------------------------
 */

static void test_a_failed_transmission_is_charged_never_repeated_and_leaves_idle(void)
{
    static const stark_err_t k_errs[] = {STARK_ERR_TIMEOUT, STARK_ERR_IO, STARK_ERR_STATE,
                                         STARK_ERR_NO_MEM};
    for (size_t i = 0; i < sizeof k_errs / sizeof k_errs[0]; i++) {
        start_at(10000);
        ready_to_send();
        load_scene(FAKE_SUBGHZ_SCENE_PWM24);
        s_tx_err = k_errs[i];
        TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
        uint32_t before = s_now;
        go();
        TEST_ASSERT_EQUAL(1, s_tx_calls);
        TEST_ASSERT_EQUAL(1, fake_cc1101_stats()->tx_strobes);
        TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->gdo0_symbols);
        TEST_ASSERT_TRUE_MESSAGE(s_now - before < 202u + 100u + 100u, "bounded by the timeout");
        TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_TX_FAILED));
        TEST_ASSERT_EQUAL(0, count_events(STARK_SUBGHZ_EVT_TX_DONE));
        TEST_ASSERT_EQUAL(k_errs[i], s_txlog[s_ntx - 1u].err);
        TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, s_txlog[s_ntx - 1u].verdict);
        stark_subghz_status_t st;
        subghz_svc_status(&s_svc, &st);
        TEST_ASSERT_TRUE(st.tx_done);
        TEST_ASSERT_EQUAL(k_errs[i], st.tx_err);
        assert_idle();
        TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc)); /* the radio is fine: only the send failed */
        run_for(120000);                            /* no retry, ever */
        TEST_ASSERT_EQUAL(1, s_tx_calls);
        TEST_ASSERT_EQUAL(1, fake_cc1101_stats()->tx_strobes);
        TEST_ASSERT_EQUAL_UINT32(360000u - 202u, subghz_txpolicy_budget_left_ms(&s_svc.pol, s_now));
        assert_power_and_band_held();
    }
    /* and the off-time of a failed one is as long as a good one's */
    start_at(10000);
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    s_tx_err = STARK_ERR_IO;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    go();
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, send());
}

static void test_a_failure_at_every_transfer_leaves_the_radio_idle(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    s_xfer_n = 0;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    subghz_svc_step(&s_svc);
    unsigned total = s_xfer_n;
    TEST_ASSERT_TRUE(total > 80 && total < 120);
    for (unsigned k = 1; k <= total; k++) {
        start_at(10000);
        ready_to_send();
        load_scene(FAKE_SUBGHZ_SCENE_PWM24);
        s_xfer_n = 0;
        s_fail_at = k;
        TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
        run_for(20000); /* the send, a drop if it came to that, and the module identified again */
        TEST_ASSERT_TRUE_MESSAGE(subghz_svc_ready(&s_svc), "the module is back");
        assert_idle();
        const fake_cc1101_stats_t *f = fake_cc1101_stats();
        TEST_ASSERT_TRUE(s_tx_calls <= 1);
        TEST_ASSERT_TRUE(f->tx_strobes <= 1);
        TEST_ASSERT_TRUE_MESSAGE(f->gdo0_symbols == 0 || f->gdo0_symbols == 192, "all or none");
        TEST_ASSERT_TRUE_MESSAGE(f->gdo0_symbols == 0 || f->tx_strobes == 1, "no STX, no symbols");
        TEST_ASSERT_EQUAL(0, f->gdo0_refused);
        TEST_ASSERT_EQUAL_MESSAGE(1, count_tx_logs(SUBGHZ_TX_OK), "one attempt, logged");
        TEST_ASSERT_EQUAL(0, count_tx_logs(SUBGHZ_TX_REFUSE_BUDGET));
        TEST_ASSERT_TRUE(
            count_events(STARK_SUBGHZ_EVT_TX_DONE) + count_events(STARK_SUBGHZ_EVT_TX_FAILED) <= 1);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(
            360000u - 202u, subghz_txpolicy_budget_left_ms(&s_svc.pol, s_now), "charged once");
        assert_power_and_band_held();
        TEST_ASSERT_TRUE_MESSAGE(s_xfer_n < 600, "bounded");
    }
}

static void test_a_bus_that_dies_from_any_transfer_on_is_bounded_and_recovers(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    s_xfer_n = 0;
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    subghz_svc_step(&s_svc);
    unsigned total = s_xfer_n;
    for (unsigned k = 1; k <= total; k++) {
        start_at(10000);
        ready_to_send();
        load_scene(FAKE_SUBGHZ_SCENE_PWM24);
        s_xfer_n = 0;
        s_fail_from = k;
        TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
        run_for(4000);
        TEST_ASSERT_FALSE_MESSAGE(subghz_svc_ready(&s_svc), "a dead bus drops the module");
        TEST_ASSERT_TRUE_MESSAGE(s_xfer_n < 300, "a dead bus is not polled in a loop");
        TEST_ASSERT_TRUE(fake_cc1101_stats()->tx_strobes <= 1);
        TEST_ASSERT_TRUE(s_tx_calls <= 1);
        TEST_ASSERT_TRUE(fake_cc1101_stats()->gdo0_symbols == 0 ||
                         fake_cc1101_stats()->tx_strobes == 1);
        s_fail_from = 0; /* the bus comes back: the radio is reset and idle again */
        run_for(20000);
        TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
        assert_idle();
        TEST_ASSERT_TRUE(s_tx_calls <= 1);
        TEST_ASSERT_TRUE(fake_cc1101_stats()->tx_strobes <= 1);
        assert_power_and_band_held();
    }
}

static void test_a_chip_that_never_enters_tx_never_gets_a_symbol(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    fake_cc1101_fault(FAKE_CC1101_STUCK, -1); /* MARCSTATE never changes: STX never takes */
    go();
    TEST_ASSERT_EQUAL(1, fake_cc1101_stats()->tx_strobes); /* STX was tried ... */
    TEST_ASSERT_EQUAL(0, s_tx_calls);                      /* ... and no symbol followed it */
    TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->gdo0_symbols);
    TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_TX_FAILED));
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, s_txlog[s_ntx - 1u].err);
    assert_idle();
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);
    run_for(60000);
    TEST_ASSERT_EQUAL(0, s_tx_calls); /* nothing retried it */
    TEST_ASSERT_EQUAL(1, fake_cc1101_stats()->tx_strobes);
    TEST_ASSERT_EQUAL_UINT32(360000u - 202u, subghz_txpolicy_budget_left_ms(&s_svc.pol, s_now));
}

/* ---- the chip is read back before STX
 * ------------------------------------------------------------ */

static void inject_out_of_band_frequency(void)
{
    uint8_t tx[4] = {CC1101_FREQ2 | CC1101_HDR_BURST, 0x10, 0xA7, 0x61}; /* 433.00 MHz */
    uint8_t rx[4];
    (void)fake_cc1101_xfer(tx, rx, sizeof tx);
}

static void inject_hot_patable(void)
{
    uint8_t tx[9] = {CC1101_PATABLE | CC1101_HDR_BURST, 0x00, 0xC6, 0, 0, 0, 0, 0, 0};
    uint8_t rx[9];
    (void)fake_cc1101_xfer(tx, rx, sizeof tx);
}

static void inject_one_step_over(void)
{
    uint8_t tx[9] = {CC1101_PATABLE | CC1101_HDR_BURST, 0x00, 0x61, 0, 0, 0, 0, 0, 0};
    uint8_t rx[9];
    (void)fake_cc1101_xfer(tx, rx, sizeof tx);
}

static void expect_failed_before_stx(void)
{
    go();
    TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->tx_strobes);
    TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->gdo0_symbols);
    TEST_ASSERT_EQUAL(0, s_tx_calls);
    TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_TX_FAILED));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, s_txlog[s_ntx - 1u].err);
    assert_idle();
    TEST_ASSERT_EQUAL_UINT32(360000u - 202u, subghz_txpolicy_budget_left_ms(&s_svc.pol, s_now));
}

static void test_the_band_is_read_back_from_the_chip_before_stx(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    /* the second read of FREQ2..0 is the transmit path's (the first is the configuration's) */
    s_before = inject_out_of_band_frequency;
    s_before_hdr = CC1101_HDR_READ | CC1101_HDR_BURST | CC1101_FREQ2;
    s_before_nth = 2;
    s_before_seen = 0;
    expect_failed_before_stx();
}

static void test_a_flipped_bit_in_the_frequency_read_back_stops_the_send(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    s_flip_hdr = CC1101_HDR_READ | CC1101_HDR_BURST | CC1101_FREQ2;
    s_flip_nth = 2;
    s_flip_seen = 0;
    expect_failed_before_stx();
}

static void test_the_power_table_is_read_back_from_the_chip_before_stx(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    s_before = inject_hot_patable; /* the chip's power-on default, +10 dBm: more than the cap */
    s_before_hdr = CC1101_HDR_READ | CC1101_HDR_BURST | CC1101_PATABLE;
    s_before_nth = 1;
    s_before_seen = 0;
    expect_failed_before_stx();

    start_at(10000);
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    s_before = inject_one_step_over; /* 0x61: one step over the cap is enough to refuse */
    s_before_hdr = CC1101_HDR_READ | CC1101_HDR_BURST | CC1101_PATABLE;
    s_before_nth = 1;
    s_before_seen = 0;
    expect_failed_before_stx();

    start_at(10000);
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    s_flip_hdr = CC1101_HDR_READ | CC1101_HDR_BURST | CC1101_PATABLE;
    s_flip_nth = 1;
    s_flip_seen = 0;
    expect_failed_before_stx(); /* a flipped bit in the table's read-back */
}

/* ---- the duty window outlives what could reset it -------------------------------------------- */

static void test_the_duty_window_survives_a_lost_module_and_the_clock_wrapping(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    go();
    TEST_ASSERT_EQUAL(1, s_tx_calls);
    fake_24c02_remove(A0); /* unplug and plug back at once: not a way to start a fresh window */
    for (int i = 0; i < 2; i++) {
        (void)module_core_tick(&s_mc);
    }
    run_for(100);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    module_in();
    run_for(300);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, check());
    TEST_ASSERT_EQUAL_UINT32(360000u - 202u, s_info.budget_left_ms);
    run_for(9u * 202u);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check());
    TEST_ASSERT_EQUAL_UINT32(360000u - 202u, s_info.budget_left_ms);

    /* the same across the 32-bit clock's wrap: a transmission straddles it */
    start_at(0xFFFFFFFFu - 20000u);
    ready();
    s_now = 0xFFFFFFFFu - 100u; /* 19.9 s after the service started: the boot lock is over */
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    go();
    TEST_ASSERT_EQUAL(1, s_tx_calls);
    TEST_ASSERT_TRUE_MESSAGE(s_now < 0x1000u, "the clock wrapped during the transmission");
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, check());
    TEST_ASSERT_TRUE(s_info.wait_ms > 1000 && s_info.wait_ms <= 9u * 202u);
    TEST_ASSERT_EQUAL_UINT32(360000u - 202u, s_info.budget_left_ms);
    run_for(9u * 202u + 300u);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check());
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    go();
    TEST_ASSERT_EQUAL(2, s_tx_calls);
    TEST_ASSERT_EQUAL_UINT32(360000u - 404u, subghz_txpolicy_budget_left_ms(&s_svc.pol, s_now));
}

static void test_the_budget_is_spent_in_steps_of_what_was_sent(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    for (unsigned i = 1; i <= 5; i++) {
        TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
        go();
        TEST_ASSERT_EQUAL(i, s_tx_calls);
        run_for(9u * 202u + 100u);
        TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, check());
        TEST_ASSERT_EQUAL_UINT32(360000u - 202u * i, s_info.budget_left_ms);
    }
}

/* ---- the radio is usable after a send
 * --------------------------------------------------------------- */

static void test_the_meter_and_the_scan_work_after_a_send(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, send());
    go();
    fake_cc1101_air(433920000u, -40);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    run_for(400);
    stark_subghz_status_t st;
    subghz_svc_status(&s_svc, &st);
    TEST_ASSERT_TRUE(st.valid);
    TEST_ASSERT_EQUAL(-80, st.dbm_x2);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    run_for(2000);
    subghz_svc_status(&s_svc, &st);
    TEST_ASSERT_TRUE(st.scan_done);
    subghz_svc_stop(&s_svc);
    run_for(500);
    assert_idle();
    TEST_ASSERT_EQUAL(1, s_tx_calls);
}

static void test_names_are_copied_and_bounded_and_info_is_optional(void)
{
    ready_to_send();
    load_scene(FAKE_SUBGHZ_SCENE_PWM24);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, subghz_svc_tx_check(&s_svc, &s_raw, "rf_0001.rf", NULL));
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, subghz_svc_tx(&s_svc, &s_raw, "a_name_longer_than_it_can_be.rf",
                                                  hash(), NULL));
    go();
    TEST_ASSERT_EQUAL_STRING("a_name_longer_t", s_txlog[0].name); /* 15 characters at most */
    run_for(9u * 202u + 100u);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_OK, subghz_svc_tx(&s_svc, &s_raw, NULL, hash(), NULL));
    go();
    TEST_ASSERT_EQUAL_STRING("", s_txlog[1].name);
    TEST_ASSERT_EQUAL(SUBGHZ_TX_REFUSE_OFFTIME, subghz_svc_tx_check(&s_svc, &s_raw, NULL, NULL));
    TEST_ASSERT_EQUAL_STRING("", s_txlog[2].name);
}

/* ---- a sender that asks as fast as it may
 * ------------------------------------------------------------- */

#define SIM_MAX_TX 4096

typedef struct {
    uint32_t start, end;
} span_t;

static span_t s_sim[SIM_MAX_TX];
static size_t s_sim_n;

/* Asks every 100 ms for two hours; checks what actually went out against the limits, independently.
 */
static void greedy(const subghz_sym_t *v, size_t n, uint32_t lock_start)
{
    load_syms(v, n);
    uint32_t span_ms = 0;
    for (size_t i = 0; i < n; i++) {
        span_ms += (uint32_t)(v[i] < 0 ? -v[i] : v[i]);
    }
    span_ms = (span_ms + 999u) / 1000u;
    s_sim_n = 0;
    uint32_t t_end = s_now + 7200u * 1000u;
    unsigned seen = 0;
    while ((int32_t)(t_end - s_now) > 0) {
        if (send() == SUBGHZ_TX_OK) {
            TEST_ASSERT_TRUE(s_sim_n < SIM_MAX_TX);
            run_for(10);
            TEST_ASSERT_EQUAL_MESSAGE(seen + 1u, s_tx_calls,
                                      "one transmission per accepted request");
            seen = s_tx_calls;
            s_sim[s_sim_n].start = s_tx_start_ms;
            s_sim[s_sim_n].end = s_tx_start_ms + span_ms;
            s_sim_n++;
        }
        run_for(100);
    }
    TEST_ASSERT_TRUE(s_sim_n > 10);
    TEST_ASSERT_TRUE_MESSAGE(s_sim[0].start - lock_start >= SUBGHZ_TX_BOOT_LOCK_MS,
                             "the boot lock");
    for (size_t i = 0; i < s_sim_n; i++) {
        if (i > 0) {
            TEST_ASSERT_TRUE_MESSAGE(s_sim[i].start - s_sim[i - 1].end >=
                                         SUBGHZ_TX_OFF_FACTOR *
                                             (s_sim[i - 1].end - s_sim[i - 1].start),
                                     "nine times each transmission before the next");
        }
        /* the time on air in the hour that ends where this one ends */
        uint64_t onair = 0;
        uint32_t t = s_sim[i].end;
        for (size_t j = 0; j <= i; j++) {
            uint32_t a = s_sim[j].start;
            uint32_t b = s_sim[j].end;
            uint32_t lo = t - SUBGHZ_TX_WINDOW_MS;
            uint32_t from = (int32_t)(a - lo) > 0 ? a : lo;
            if ((int32_t)(b - from) > 0) {
                onair += b - from;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(onair <= SUBGHZ_TX_BUDGET_MS, "at most 360 s in any hour");
    }
    TEST_ASSERT_EQUAL(s_sim_n, s_tx_calls);
    TEST_ASSERT_EQUAL(s_sim_n, fake_cc1101_stats()->tx_strobes);
    assert_idle();
    assert_power_and_band_held();
}

static void test_a_greedy_sender_gets_at_most_ten_percent_of_every_hour(void)
{
    ready();
    const subghz_sym_t two_s[2] = {1000000, -1000000};
    greedy(two_s, 2, 10000);
    TEST_ASSERT_TRUE(s_sim_n >= 180 && s_sim_n <= 361); /* a 2 s send every 20 s, then the budget */
}

static void test_a_greedy_sender_with_short_files_and_a_full_ledger(void)
{
    ready();
    greedy(k_fake_scenes[0].sym, k_fake_scenes[0].n, 10000); /* 202 ms: thousands of entries */
    TEST_ASSERT_TRUE(s_sim_n > 100);
}

static void test_a_greedy_sender_across_the_clock_wrap(void)
{
    start_at(0xFFFFFFFFu - 3600u * 1000u); /* the hour's middle is the wrap */
    ready();
    const subghz_sym_t two_s[2] = {1000000, -1000000};
    greedy(two_s, 2, 0xFFFFFFFFu - 3600u * 1000u);
    TEST_ASSERT_TRUE(s_sim_n >= 180);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_request_sends_the_file_once_exactly_and_idles_the_radio);
    RUN_TEST(test_an_immediate_second_request_is_refused_for_the_off_time);
    RUN_TEST(test_the_fake_own_receiver_hears_the_replay_back_exactly);
    RUN_TEST(test_the_largest_transmissions_and_their_timeouts);
    RUN_TEST(test_no_measurement_and_no_idling_ever_transmits);
    RUN_TEST(test_refusal_no_module_and_the_boot_lock);
    RUN_TEST(test_refusal_band);
    RUN_TEST(test_refusal_length);
    RUN_TEST(test_refusal_duration);
    RUN_TEST(test_refusal_budget);
    RUN_TEST(test_refusal_file);
    RUN_TEST(test_refusal_a_file_that_is_not_what_was_confirmed);
    RUN_TEST(test_refusal_busy_one_request_at_a_time);
    RUN_TEST(test_the_transmit_path_checks_everything_again);
    RUN_TEST(test_stop_and_any_other_request_drop_a_pending_send);
    RUN_TEST(test_a_send_during_a_capture_stops_the_receiver_first);
    RUN_TEST(test_a_failed_transmission_is_charged_never_repeated_and_leaves_idle);
    RUN_TEST(test_a_failure_at_every_transfer_leaves_the_radio_idle);
    RUN_TEST(test_a_bus_that_dies_from_any_transfer_on_is_bounded_and_recovers);
    RUN_TEST(test_a_chip_that_never_enters_tx_never_gets_a_symbol);
    RUN_TEST(test_the_band_is_read_back_from_the_chip_before_stx);
    RUN_TEST(test_a_flipped_bit_in_the_frequency_read_back_stops_the_send);
    RUN_TEST(test_the_power_table_is_read_back_from_the_chip_before_stx);
    RUN_TEST(test_the_duty_window_survives_a_lost_module_and_the_clock_wrapping);
    RUN_TEST(test_the_budget_is_spent_in_steps_of_what_was_sent);
    RUN_TEST(test_the_meter_and_the_scan_work_after_a_send);
    RUN_TEST(test_names_are_copied_and_bounded_and_info_is_optional);
    RUN_TEST(test_a_greedy_sender_gets_at_most_ten_percent_of_every_hour);
    RUN_TEST(test_a_greedy_sender_with_short_files_and_a_full_ledger);
    RUN_TEST(test_a_greedy_sender_across_the_clock_wrap);
    return UNITY_END();
}
