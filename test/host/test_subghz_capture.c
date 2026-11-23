/*
 * test_subghz_capture.c — the Sub-GHz service's raw capture (TASKS.md STARK-0603,
 * ADR-0022) over the real module core, a fake 24C02 and the fake CC1101 playing the
 * spec-derived scenes on GDO2: the symbols kept equal the generator's (count,
 * fingerprint, duration), the limits end a capture by themselves, the radio and the
 * receiver are never left running, and every failure is bounded and fails closed.
 */
#include <limits.h>
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
static unsigned s_rx_starts, s_rx_stops, s_rx_takes;
static bool s_rx_on;
static stark_err_t s_rx_start_err;
static size_t s_take_extra; /* a misbehaving transport reports this many more than it wrote */
static uint8_t s_fail_hdr;  /* transfers that start with this byte fail ... */
static unsigned s_fail_count;
static int s_evts[256];
static size_t s_nevts;

typedef struct {
    subghz_log_t what;
    unsigned a, b;
} logged_t;
static logged_t s_log[64];
static size_t s_nlog;

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
    if (s_fail_count > 0 && tx[0] == s_fail_hdr) {
        if (s_fail_count != UINT_MAX) {
            s_fail_count--;
        }
        return STARK_ERR_IO;
    }
    return fake_cc1101_xfer(tx, rx, len);
}

static stark_err_t io_rx_start(void *ctx)
{
    (void)ctx;
    s_rx_starts++;
    if (s_rx_start_err == STARK_OK) {
        s_rx_on = true;
    }
    return s_rx_start_err;
}

static size_t io_rx_take(void *ctx, subghz_sym_t *out, size_t max, uint32_t timeout_ms)
{
    (void)ctx;
    s_rx_takes++;
    TEST_ASSERT_TRUE_MESSAGE(s_rx_on, "symbols asked of a receiver that is not running");
    TEST_ASSERT_TRUE(max == SUBGHZ_TAKE_MAX && timeout_ms == SUBGHZ_CAPTURE_POLL_MS);
    size_t n = fake_cc1101_gdo2_take(out, max);
    if (n == 0) {
        s_now += timeout_ms; /* the wait */
    }
    return n + s_take_extra;
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

static const subghz_svc_io_t k_io = {io_list,  io_claim, io_release,  io_lease_valid, io_open,
                                     io_close, io_xfer,  io_rx_start, io_rx_take,     io_rx_stop,
                                     io_now,   io_delay, io_log,      io_changed,     io_notify,
                                     NULL,     NULL,     NULL};

void setUp(void)
{
    fake_24c02_reset();
    fake_cc1101_reset();
    module_core_init(&s_mc, &k_mio, k_board, 2);
    subghz_svc_init(&s_svc, &k_io);
    s_now = 10000;
    s_open = false;
    s_rx_starts = s_rx_stops = s_rx_takes = 0;
    s_rx_on = false;
    s_rx_start_err = STARK_OK;
    s_take_extra = 0;
    s_fail_count = 0;
    s_nevts = 0;
    s_nlog = 0;
    for (size_t i = 0; i < sizeof s_buf / sizeof s_buf[0]; i++) {
        s_buf[i] = 0x7A7A7A7A; /* a sentinel: what the service never wrote */
    }
}
void tearDown(void)
{}

static void ready(void)
{
    fake_24c02_put(A0, k_fake_modules[FAKE_MODULE_CC1101_MODULE - 1].image, MODULE_DESC_SIZE);
    for (int i = 0; i < 2; i++) {
        (void)module_core_tick(&s_mc);
    }
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

static void status(stark_subghz_status_t *st)
{
    subghz_svc_status(&s_svc, st);
}

static size_t count_events(int evt)
{
    size_t n = 0;
    for (size_t i = 0; i < s_nevts; i++) {
        n += s_evts[i] == evt ? 1u : 0u;
    }
    return n;
}

static size_t count_logs(subghz_log_t what)
{
    size_t n = 0;
    for (size_t i = 0; i < s_nlog; i++) {
        n += s_log[i].what == what ? 1u : 0u;
    }
    return n;
}

static const logged_t *find_log(subghz_log_t what)
{
    for (size_t i = 0; i < s_nlog; i++) {
        if (s_log[i].what == what) {
            return &s_log[i];
        }
    }
    TEST_FAIL_MESSAGE("log line missing");
    return NULL;
}

static void assert_quiet_radio(void)
{
    TEST_ASSERT_EQUAL_HEX8(CC1101_MARC_IDLE, fake_cc1101_stats()->marcstate);
    TEST_ASSERT_FALSE(s_rx_on);
    TEST_ASSERT_EQUAL(s_rx_starts, s_rx_stops);
}

static uint8_t mod_format(void)
{
    return (uint8_t)(fake_cc1101_stats()->reg[CC1101_MDMCFG2] & 0x70u);
}

/* The preset's MDMCFG2 modulation field, from the register profile itself. */
static uint8_t preset_mod_format(cc1101_preset_t p)
{
    const cc1101_reg_t *regs;
    size_t n = cc1101_preset_regs(p, &regs);
    for (size_t i = 0; i < n; i++) {
        if (regs[i].addr == CC1101_MDMCFG2) {
            return (uint8_t)(regs[i].value & 0x70u);
        }
    }
    TEST_FAIL_MESSAGE("no MDMCFG2");
    return 0;
}

/* Plays scene `id` (1-based), captures it until it ends by itself or `finish_after` ms. */
static void capture_scene(int id, bool finish)
{
    const fake_subghz_scene_t *sc = &k_fake_scenes[id - 1];
    fake_cc1101_scene(sc->sym, sc->n);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf,
                                                   SUBGHZ_RAW_MAX_SYMBOLS));
    run_for(400);
    if (finish) {
        subghz_svc_capture_finish(&s_svc);
        run_for(100);
    }
}

/* ---- requests -------------------------------------------------------------------- */

static void test_requests_need_a_ready_module_and_sane_arguments(void)
{
    TEST_ASSERT_EQUAL(STARK_ERR_STATE,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 16));
    subghz_svc_capture_finish(&s_svc); /* nothing to finish: always safe */
    ready();
    unsigned xfers = fake_cc1101_stats()->transfers;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_COUNT, 433920000u, s_buf, 16));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, NULL, 16));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433049999u, s_buf, 16));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_2FSK, 868000000u, s_buf, 16));
    TEST_ASSERT_EQUAL(xfers, fake_cc1101_stats()->transfers); /* nothing reached the chip */
    run_for(100);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_IDLE, st.mode);
    TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->out_of_band);
    TEST_ASSERT_EQUAL(0, s_rx_starts);
}

/* ---- the pipeline ------------------------------------------------------------------ */

static void test_a_scene_is_captured_exactly_through_the_service(void)
{
    for (int id = 1; id <= 3; id++) { /* pwm, ppm (with its silence), manchester */
        setUp();
        ready();
        const fake_subghz_scene_t *sc = &k_fake_scenes[id - 1];
        capture_scene(id, true);
        stark_subghz_status_t st;
        status(&st);
        TEST_ASSERT_EQUAL_MESSAGE(STARK_SUBGHZ_CAPTURE, st.mode, sc->name);
        TEST_ASSERT_TRUE(st.capture_done);
        TEST_ASSERT_EQUAL(sc->kept_n, st.symbols);
        TEST_ASSERT_EQUAL_UINT32(sc->kept_us, st.span_us);
        TEST_ASSERT_EQUAL_UINT32(433920000u, st.hz);
        TEST_ASSERT_EQUAL_HEX32(sc->kept_hash, subghz_raw_hash(s_buf, st.symbols));
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0x7A7A7A7Au, (uint32_t)s_buf[st.symbols], "past the end");
        const logged_t *l = find_log(SUBGHZ_LOG_CAPTURE_DONE);
        TEST_ASSERT_EQUAL(sc->kept_n, l->a);
        TEST_ASSERT_EQUAL_HEX32(sc->kept_hash, l->b);
        l = find_log(SUBGHZ_LOG_CAPTURE_START);
        TEST_ASSERT_EQUAL_UINT32(433920000u, l->a);
        TEST_ASSERT_EQUAL(SUBGHZ_MOD_OOK, l->b);
        TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_CAPTURE_DONE));
        assert_quiet_radio();
        TEST_ASSERT_EQUAL(1, s_rx_starts);
        TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    }
}

static void test_scene_timing_matches_per_symbol(void)
{
    ready();
    capture_scene(FAKE_SUBGHZ_SCENE_PWM24, true);
    stark_subghz_status_t st;
    status(&st);
    const fake_subghz_scene_t *sc = &k_fake_scenes[FAKE_SUBGHZ_SCENE_PWM24 - 1];
    TEST_ASSERT_EQUAL(sc->n, st.symbols);
    for (size_t i = 0; i < st.symbols;
         i++) { /* every symbol, to the microsecond (the AC allows ±1) */
        TEST_ASSERT_EQUAL_INT32(sc->sym[i], s_buf[i]);
    }
}

static void test_the_chip_is_tuned_and_configured_for_the_capture(void)
{
    ready();
    const fake_subghz_scene_t *sc = &k_fake_scenes[0];
    fake_cc1101_scene(sc->sym, sc->n);
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 434250000u, s_buf, 2048));
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_CAPTURE, st.mode);
    TEST_ASSERT_FALSE(st.capture_done);
    TEST_ASSERT_EQUAL(0, st.symbols);
    run_for(60);
    TEST_ASSERT_EQUAL_HEX8(CC1101_MARC_RX, fake_cc1101_stats()->marcstate);
    TEST_ASSERT_TRUE(fake_cc1101_stats()->tuned_hz > 434249000u &&
                     fake_cc1101_stats()->tuned_hz < 434251000u);
    TEST_ASSERT_EQUAL_HEX8(preset_mod_format(CC1101_PRESET_OOK_RX), mod_format());
    TEST_ASSERT_EQUAL(1, s_rx_starts);
    TEST_ASSERT_TRUE(s_rx_on);
    subghz_svc_stop(&s_svc);
    run_for(60);
    assert_quiet_radio();
}

static void test_a_2fsk_capture_uses_its_profile_and_leaves_the_usual_one(void)
{
    ready();
    const fake_subghz_scene_t *sc = &k_fake_scenes[0];
    fake_cc1101_scene(sc->sym, sc->n);
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_2FSK, 433920000u, s_buf, 2048));
    run_for(60);
    TEST_ASSERT_EQUAL_HEX8(preset_mod_format(CC1101_PRESET_FSK2_RX), mod_format());
    TEST_ASSERT_NOT_EQUAL(preset_mod_format(CC1101_PRESET_OOK_RX),
                          preset_mod_format(CC1101_PRESET_FSK2_RX));
    subghz_svc_capture_finish(&s_svc);
    run_for(60);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_TRUE(st.capture_done);
    TEST_ASSERT_EQUAL(sc->kept_n, st.symbols);
    TEST_ASSERT_EQUAL_HEX8(preset_mod_format(CC1101_PRESET_OOK_RX), mod_format()); /* the meter's */
    assert_quiet_radio();
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u)); /* and it still works */
    run_for(200);
    status(&st);
    TEST_ASSERT_TRUE(st.valid);
    subghz_svc_stop(&s_svc);
    run_for(60);
    assert_quiet_radio();
}

/* ---- limits ---------------------------------------------------------------------- */

static void test_2048_symbols_end_a_capture_by_themselves(void)
{
    ready();
    capture_scene(FAKE_SUBGHZ_SCENE_DENSE, false);
    stark_subghz_status_t st;
    status(&st);
    const fake_subghz_scene_t *sc = &k_fake_scenes[FAKE_SUBGHZ_SCENE_DENSE - 1];
    TEST_ASSERT_TRUE(st.capture_done);
    TEST_ASSERT_EQUAL(2048, st.symbols);
    TEST_ASSERT_EQUAL_HEX32(sc->kept_hash, subghz_raw_hash(s_buf, 2048));
    TEST_ASSERT_EQUAL_HEX32(0x7A7A7A7Au, (uint32_t)s_buf[2048]);
    TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_CAPTURE_DONE));
    assert_quiet_radio();
}

static void test_two_seconds_end_a_capture_by_themselves(void)
{
    ready();
    capture_scene(FAKE_SUBGHZ_SCENE_SLOW, false);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_TRUE(st.capture_done);
    TEST_ASSERT_EQUAL(7, st.symbols);
    TEST_ASSERT_EQUAL_UINT32(2000000u, st.span_us);
    TEST_ASSERT_EQUAL(200000, s_buf[6]); /* the last symbol is cut to fit */
    assert_quiet_radio();
}

static void test_a_smaller_buffer_limits_the_capture(void)
{
    ready();
    const fake_subghz_scene_t *sc = &k_fake_scenes[0];
    fake_cc1101_scene(sc->sym, sc->n);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 10));
    run_for(200);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_TRUE(st.capture_done);
    TEST_ASSERT_EQUAL(10, st.symbols);
    TEST_ASSERT_EQUAL_HEX32(0x7A7A7A7Au, (uint32_t)s_buf[10]);
}

static void test_a_transport_that_overreports_is_clamped(void)
{
    ready();
    s_take_extra = 1000; /* claims more than it wrote: never more than the service's room is read */
    const fake_subghz_scene_t *sc = &k_fake_scenes[0];
    fake_cc1101_scene(sc->sym, sc->n);
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 2048));
    run_for(100);
    subghz_svc_stop(&s_svc);
    run_for(60);
    assert_quiet_radio(); /* (ASan: no read past the scratch buffer) */
}

/* ---- ending --------------------------------------------------------------------------- */

static void test_finishing_with_nothing_heard_keeps_nothing(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 2048));
    run_for(100);
    subghz_svc_capture_finish(&s_svc);
    run_for(100);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_TRUE(st.capture_done);
    TEST_ASSERT_EQUAL(0, st.symbols);
    TEST_ASSERT_EQUAL_HEX32(0x811c9dc5u, find_log(SUBGHZ_LOG_CAPTURE_DONE)->b);
    TEST_ASSERT_EQUAL(0, count_events(STARK_SUBGHZ_EVT_CAPTURE)); /* nothing changed: no progress */
    TEST_ASSERT_EQUAL_HEX32(0x7A7A7A7Au, (uint32_t)s_buf[0]);
    assert_quiet_radio();
    subghz_svc_capture_finish(&s_svc); /* a finished capture is not finished again */
    run_for(100);
    TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_CAPTURE_DONE));
}

static void test_finishing_at_once_still_starts_and_ends_cleanly(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 2048));
    TEST_ASSERT_EQUAL(0, s_rx_starts); /* the request alone does nothing: the next round does */
    subghz_svc_capture_finish(&s_svc);
    run_for(100);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_TRUE(st.capture_done);
    assert_quiet_radio();
}

static void test_progress_is_throttled(void)
{
    ready();
    const fake_subghz_scene_t *sc = &k_fake_scenes[FAKE_SUBGHZ_SCENE_PWM24 - 1];
    fake_cc1101_scene(sc->sym, sc->n);
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 2048));
    run_for(60); /* the symbols are in, but the first 100 ms have not passed */
    TEST_ASSERT_EQUAL(0, count_events(STARK_SUBGHZ_EVT_CAPTURE));
    run_for(100);
    TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_CAPTURE));
    run_for(500); /* nothing new: nothing more */
    TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_CAPTURE));
    subghz_svc_stop(&s_svc);
    run_for(60);
}

static void test_stop_ends_a_capture_and_the_buffer_is_not_touched_again(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 2048));
    run_for(60);
    TEST_ASSERT_TRUE(s_rx_on);
    subghz_svc_stop(&s_svc);
    const fake_subghz_scene_t *sc = &k_fake_scenes[0];
    fake_cc1101_scene(sc->sym, sc->n); /* a signal arrives after the stop */
    run_for(200);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_IDLE, st.mode);
    TEST_ASSERT_EQUAL_HEX32(0x7A7A7A7Au, (uint32_t)s_buf[0]); /* the buffer is the caller's again */
    assert_quiet_radio();
    TEST_ASSERT_EQUAL(1, count_logs(SUBGHZ_LOG_IDLE));
}

static void test_starting_again_replaces_the_capture(void)
{
    ready();
    capture_scene(FAKE_SUBGHZ_SCENE_PWM24, true);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_TRUE(st.capture_done);
    const fake_subghz_scene_t *sc = &k_fake_scenes[FAKE_SUBGHZ_SCENE_MANCHESTER32 - 1];
    fake_cc1101_scene(sc->sym, sc->n);
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 2048));
    status(&st);
    TEST_ASSERT_FALSE(st.capture_done);
    TEST_ASSERT_EQUAL(0, st.symbols);
    run_for(60);
    /* and once more while it runs: the receiver is restarted, not doubled */
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 2048));
    run_for(60);
    TEST_ASSERT_TRUE(s_rx_on);
    TEST_ASSERT_EQUAL(s_rx_starts, s_rx_stops + 1);
    subghz_svc_capture_finish(&s_svc);
    run_for(60);
    status(&st);
    TEST_ASSERT_EQUAL(sc->kept_n, st.symbols);
    TEST_ASSERT_EQUAL_HEX32(sc->kept_hash, subghz_raw_hash(s_buf, st.symbols));
    assert_quiet_radio();
}

static void test_another_measurement_stops_the_receiver(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 2048));
    run_for(60);
    TEST_ASSERT_TRUE(s_rx_on);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    run_for(100);
    TEST_ASSERT_FALSE(s_rx_on);
    TEST_ASSERT_EQUAL(s_rx_starts, s_rx_stops);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_METER, st.mode);
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 2048));
    run_for(60);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    run_for(600);
    TEST_ASSERT_FALSE(s_rx_on);
    TEST_ASSERT_EQUAL(s_rx_starts, s_rx_stops);
    status(&st);
    TEST_ASSERT_TRUE(st.scan_done);
    assert_quiet_radio();
}

/* ---- faults ---------------------------------------------------------------------------- */

static void test_a_module_lost_during_a_capture(void)
{
    ready();
    const fake_subghz_scene_t *sc = &k_fake_scenes[0];
    fake_cc1101_scene(sc->sym, sc->n);
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 2048));
    run_for(60);
    TEST_ASSERT_TRUE(s_rx_on);
    fake_24c02_remove(A0); /* the module leaves its slot */
    for (int i = 0; i < 3; i++) {
        (void)module_core_tick(&s_mc);
    }
    run_for(300);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_FALSE(s_rx_on);
    TEST_ASSERT_EQUAL(s_rx_starts, s_rx_stops);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_IDLE, st.mode);
    TEST_ASSERT_EQUAL(STARK_ERR_STATE,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 16));
}

static void test_a_receiver_that_will_not_start_is_bounded_and_fails_closed(void)
{
    ready();
    s_rx_start_err = STARK_ERR_IO;
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 2048));
    subghz_svc_step(&s_svc);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc)); /* one miss: tried again next round */
    subghz_svc_step(&s_svc);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc)); /* two in a row: dropped, like a failed check */
    TEST_ASSERT_EQUAL(2, s_rx_starts);
    TEST_ASSERT_EQUAL(1, count_logs(SUBGHZ_LOG_ERROR));
    TEST_ASSERT_EQUAL_HEX8(CC1101_MARC_IDLE, fake_cc1101_stats()->marcstate);
    TEST_ASSERT_FALSE(s_rx_on);
}

static void test_a_chip_that_will_not_receive_is_bounded_and_fails_closed(void)
{
    ready();
    s_fail_hdr = CC1101_SRX; /* the strobe that starts reception fails */
    s_fail_count = UINT_MAX;
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 2048));
    for (int i = 0; i < 4 && subghz_svc_ready(&s_svc); i++) {
        subghz_svc_step(&s_svc);
    }
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL(
        0, s_rx_starts); /* the receiver never ran against a chip that was not receiving */
    TEST_ASSERT_FALSE(s_rx_on);
}

static void test_a_radio_that_will_not_go_idle_at_the_end_is_dropped(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 2048));
    run_for(60);
    s_fail_hdr = CC1101_SIDLE;
    s_fail_count = UINT_MAX;
    subghz_svc_capture_finish(&s_svc);
    subghz_svc_step(&s_svc);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_FALSE(st.capture_done); /* not announced while the chip may still be receiving */
    TEST_ASSERT_EQUAL(0, count_events(STARK_SUBGHZ_EVT_CAPTURE_DONE));
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_FALSE(s_rx_on); /* the receiver is stopped regardless */
    subghz_svc_step(&s_svc);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL(0, count_events(STARK_SUBGHZ_EVT_CAPTURE_DONE));
    TEST_ASSERT_EQUAL(s_rx_starts, s_rx_stops);
}

static void test_the_usual_profile_that_cannot_be_restored_is_dropped(void)
{
    ready();
    const fake_subghz_scene_t *sc = &k_fake_scenes[0];
    fake_cc1101_scene(sc->sym, sc->n);
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_2FSK, 433920000u, s_buf, 2048));
    run_for(60);
    s_fail_hdr = CC1101_IOCFG2; /* the profile's first register write fails */
    s_fail_count = UINT_MAX;
    subghz_svc_capture_finish(&s_svc);
    subghz_svc_step(&s_svc);
    subghz_svc_step(&s_svc);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL(0, count_events(STARK_SUBGHZ_EVT_CAPTURE_DONE));
    TEST_ASSERT_FALSE(s_rx_on);
}

static void test_a_fault_that_clears_lets_the_capture_finish(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK,
                      subghz_svc_capture(&s_svc, SUBGHZ_MOD_OOK, 433920000u, s_buf, 2048));
    run_for(60);
    s_fail_hdr = CC1101_SIDLE;
    s_fail_count = 1; /* one failed attempt, then it works */
    subghz_svc_capture_finish(&s_svc);
    subghz_svc_step(&s_svc);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    run_for(100);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_TRUE(st.capture_done);
    TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_CAPTURE_DONE));
    assert_quiet_radio();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_requests_need_a_ready_module_and_sane_arguments);
    RUN_TEST(test_a_scene_is_captured_exactly_through_the_service);
    RUN_TEST(test_scene_timing_matches_per_symbol);
    RUN_TEST(test_the_chip_is_tuned_and_configured_for_the_capture);
    RUN_TEST(test_a_2fsk_capture_uses_its_profile_and_leaves_the_usual_one);
    RUN_TEST(test_2048_symbols_end_a_capture_by_themselves);
    RUN_TEST(test_two_seconds_end_a_capture_by_themselves);
    RUN_TEST(test_a_smaller_buffer_limits_the_capture);
    RUN_TEST(test_a_transport_that_overreports_is_clamped);
    RUN_TEST(test_finishing_with_nothing_heard_keeps_nothing);
    RUN_TEST(test_finishing_at_once_still_starts_and_ends_cleanly);
    RUN_TEST(test_progress_is_throttled);
    RUN_TEST(test_stop_ends_a_capture_and_the_buffer_is_not_touched_again);
    RUN_TEST(test_starting_again_replaces_the_capture);
    RUN_TEST(test_another_measurement_stops_the_receiver);
    RUN_TEST(test_a_module_lost_during_a_capture);
    RUN_TEST(test_a_receiver_that_will_not_start_is_bounded_and_fails_closed);
    RUN_TEST(test_a_chip_that_will_not_receive_is_bounded_and_fails_closed);
    RUN_TEST(test_a_radio_that_will_not_go_idle_at_the_end_is_dropped);
    RUN_TEST(test_the_usual_profile_that_cannot_be_restored_is_dropped);
    RUN_TEST(test_a_fault_that_clears_lets_the_capture_finish);
    return UNITY_END();
}
