/*
 * test_subghz_measure.c — the Sub-GHz service's receive-only measurements (TASKS.md
 * STARK-0602, ADR-0022): the RSSI meter and the band scan over the real module
 * core, a fake 24C02 and the fake CC1101. The meter follows the injected level
 * within 1 dB, the scan finds the injected carrier's channel, and the radio is
 * never left receiving after a stop, a finished sweep, a fault or a lost module.
 */
#include <limits.h>
#include <string.h>
#include "fake_24c02.h"
#include "fake_cc1101.h"
#include "fake_module_catalog.h"
#include "module_core.h"
#include "subghz_svc.h"
#include "unity.h"

#define A0 0x50
#define A1 0x51

static module_core_t s_mc;
static subghz_svc_t s_svc;
static uint32_t s_now;
static bool s_open;
static stark_err_t s_open_err;
static unsigned s_opens, s_closes, s_claims, s_releases, s_xfers, s_changes;
static uint8_t s_fail_hdr;        /* the transfers that start with this byte ... */
static unsigned s_fail_hdr_skip;  /* ... pass this many times first, then ... */
static unsigned s_fail_hdr_count; /* ... fail this many times (UINT_MAX: always) */
static int s_evts[64];
static size_t s_nevts;
static unsigned s_fail_from; /* 1-based xfer after open from which every xfer fails */
static unsigned s_open_xfers;
static size_t s_list_extra; /* the list call reports this many more entries than it holds */

typedef struct {
    subghz_log_t what;
    unsigned a, b;
} logged_t;
static logged_t s_log[32];
static size_t s_nlog;

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
static const module_wiring_t k_board[] = {{A0, 40, 41, 42}, {A1, -1, -1, -1}}; /* ADR-0021 §5 */

static size_t io_list(void *ctx, stark_module_entry_t *out, size_t max)
{
    (void)ctx;
    return module_core_list(&s_mc, out, max) + s_list_extra;
}

static stark_err_t io_claim(void *ctx, uint8_t slot, stark_module_lease_t *out)
{
    (void)ctx;
    s_claims++;
    return module_core_claim(&s_mc, slot, STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ, "subghz", out);
}

static void io_release(void *ctx, const stark_module_lease_t *l)
{
    (void)ctx;
    s_releases++;
    module_core_release(&s_mc, l);
}

static bool io_lease_valid(void *ctx, const stark_module_lease_t *l)
{
    (void)ctx;
    return module_core_lease_valid(&s_mc, l);
}

static stark_module_lease_t s_open_lease;

static stark_err_t io_open(void *ctx, const stark_module_lease_t *l)
{
    (void)ctx;
    s_opens++;
    if (s_open_err != STARK_OK) {
        return s_open_err;
    }
    int cs, i0, i1;
    stark_err_t err = module_core_lease_pins(&s_mc, l, &cs, &i0, &i1);
    if (err != STARK_OK) {
        return err;
    }
    TEST_ASSERT_EQUAL(40, cs); /* the lease carries slot 0's lines: no pin number in the service */
    TEST_ASSERT_EQUAL(41, i0);
    TEST_ASSERT_EQUAL(42, i1);
    s_open_lease = *l;
    s_open = true;
    s_open_xfers = 0;
    return STARK_OK;
}

static void io_close(void *ctx)
{
    (void)ctx;
    s_closes++;
    s_open = false;
}

static stark_err_t io_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    (void)ctx;
    s_xfers++;
    /* like production: a closed transport or a stale lease reaches nothing */
    if (!s_open || !module_core_lease_valid(&s_mc, &s_open_lease)) {
        return STARK_ERR_STATE;
    }
    if (s_fail_hdr_count > 0 && tx[0] == s_fail_hdr) {
        if (s_fail_hdr_skip > 0) {
            s_fail_hdr_skip--;
        } else {
            if (s_fail_hdr_count != UINT_MAX) {
                s_fail_hdr_count--;
            }
            return STARK_ERR_IO;
        }
    }
    if (s_fail_from != 0 && ++s_open_xfers >= s_fail_from) {
        return STARK_ERR_IO;
    }
    return fake_cc1101_xfer(tx, rx, len);
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
    s_changes++;
    module_core_confirm(&s_mc, STARK_CAP_SUBGHZ, ready); /* main/module_bridge.c's job */
}

static void io_notify(void *ctx, int evt)
{
    (void)ctx;
    if (s_nevts < sizeof s_evts / sizeof s_evts[0]) {
        s_evts[s_nevts++] = evt;
    }
}

/* No receiver: this file measures RSSI only (test_subghz_capture.c captures). */
static const subghz_svc_io_t k_io = {
    io_list, io_claim, io_release, io_lease_valid, io_open, io_close,   io_xfer,   NULL,
    NULL,    NULL,     io_now,     io_delay,       io_log,  io_changed, io_notify, NULL};

void setUp(void)
{
    fake_24c02_reset();
    fake_cc1101_reset();
    module_core_init(&s_mc, &k_mio, k_board, 2);
    subghz_svc_init(&s_svc, &k_io);
    s_now = 10000;
    s_open = false;
    s_open_err = STARK_OK;
    s_opens = s_closes = s_claims = s_releases = s_xfers = s_changes = 0;
    s_fail_from = 0;
    s_list_extra = 0;
    s_nlog = 0;
    s_nevts = 0;
    s_fail_hdr_count = 0;
    s_fail_hdr_skip = 0;
}
void tearDown(void)
{}

static void fit(uint8_t addr, int id)
{
    fake_24c02_put(addr, k_fake_modules[id - 1].image, MODULE_DESC_SIZE);
}

static void module_ticks(int n)
{
    for (int i = 0; i < n; i++) {
        (void)module_core_tick(&s_mc);
    }
}

static void fit_radio(void)
{
    fit(A0, FAKE_MODULE_CC1101_MODULE);
    module_ticks(2);
}

static void assert_log(size_t i, subghz_log_t what, unsigned a, unsigned b)
{
    TEST_ASSERT_TRUE_MESSAGE(i < s_nlog, "a log line is missing");
    TEST_ASSERT_EQUAL(what, s_log[i].what);
    TEST_ASSERT_EQUAL_HEX32(a, s_log[i].a);
    TEST_ASSERT_EQUAL_HEX32(b, s_log[i].b);
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

/* A ready module with an RF-quiet air. */
static void ready(void)
{
    fit_radio();
    subghz_svc_step(&s_svc);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
}

/* Steps until the clock has advanced by `ms`, as the task would (each step's own wait). */
static void run_for(uint32_t ms)
{
    uint32_t end = s_now + ms;
    while ((int32_t)(end - s_now) > 0) {
        uint32_t wait = subghz_svc_step(&s_svc);
        TEST_ASSERT_TRUE(wait >= 1 && wait <= SUBGHZ_IDLE_MS);
        s_now += wait;
    }
}

static void assert_idle_radio(void)
{
    TEST_ASSERT_EQUAL_HEX8(CC1101_MARC_IDLE, fake_cc1101_stats()->marcstate);
}

static void status(stark_subghz_status_t *st)
{
    subghz_svc_status(&s_svc, st);
}

/* ---- requests -------------------------------------------------------------- */

static void test_requests_need_a_ready_module(void)
{
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, subghz_svc_meter(&s_svc, 433920000u));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, subghz_svc_scan(&s_svc));
    subghz_svc_stop(&s_svc); /* always safe */
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_IDLE, st.mode);
    TEST_ASSERT_FALSE(st.valid);
}

static void test_a_frequency_outside_the_band_is_refused_before_the_chip(void)
{
    ready();
    unsigned xfers = s_xfers;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_svc_meter(&s_svc, 433049999u));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_svc_meter(&s_svc, 434790001u));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, subghz_svc_meter(&s_svc, 868000000u));
    TEST_ASSERT_EQUAL(xfers, s_xfers); /* nothing was sent: the band lock is in the core */
    run_for(100);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_IDLE, st.mode);
    TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->out_of_band);
}

/* ---- meter ----------------------------------------------------------------- */

static void test_the_meter_follows_the_air_within_one_db(void)
{
    ready();
    fake_cc1101_air(433920000u, -40);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_METER, st.mode);
    TEST_ASSERT_FALSE(st.valid); /* nothing measured yet */
    run_for(200);
    status(&st);
    TEST_ASSERT_TRUE(st.valid);
    TEST_ASSERT_EQUAL_UINT32(433920000u, st.hz);
    TEST_ASSERT_INT16_WITHIN(2, -80, st.dbm_x2); /* within 1 dB */
    TEST_ASSERT_EQUAL(CC1101_MARC_RX, fake_cc1101_stats()->marcstate);
    static const int levels[] = {-70, -55, -100, -30, -40};
    for (size_t i = 0; i < sizeof levels / sizeof levels[0]; i++) {
        fake_cc1101_air(433920000u, levels[i]);
        run_for(2 * SUBGHZ_METER_PERIOD_MS + 10);
        status(&st);
        TEST_ASSERT_INT16_WITHIN(2, levels[i] * 2, st.dbm_x2);
    }
    TEST_ASSERT_TRUE(count_events(STARK_SUBGHZ_EVT_RSSI) >= 6);
}

static void test_the_meter_samples_every_50_ms_and_only_announces_changes(void)
{
    ready();
    fake_cc1101_air(433920000u, -60);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    run_for(10);
    size_t after_first = count_events(STARK_SUBGHZ_EVT_RSSI);
    TEST_ASSERT_EQUAL(1, after_first);
    run_for(1000); /* an unchanging level is not an event stream */
    TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_RSSI));
    /* about twenty reads of RSSI in a second: one transfer each plus the checks */
    uint32_t before = fake_cc1101_stats()->transfers;
    run_for(500);
    uint32_t used = fake_cc1101_stats()->transfers - before;
    TEST_ASSERT_TRUE(used >= 10u); /* ten samples at least */
}

static void test_a_step_before_the_next_sample_is_due_waits_for_it(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    s_now += subghz_svc_step(&s_svc); /* tunes */
    TEST_ASSERT_EQUAL_UINT32(SUBGHZ_METER_PERIOD_MS,
                             subghz_svc_step(&s_svc)); /* the first sample */
    uint32_t xfers = fake_cc1101_stats()->transfers;
    uint32_t wait = subghz_svc_step(&s_svc); /* the same instant: nothing is due */
    TEST_ASSERT_TRUE(wait >= 1 && wait <= SUBGHZ_METER_PERIOD_MS);
    s_now += wait - 1;
    TEST_ASSERT_EQUAL_UINT32(1, subghz_svc_step(&s_svc));
    /* only the module checks (two reads) reached the chip, no sample */
    TEST_ASSERT_EQUAL_UINT32(xfers + 4u, fake_cc1101_stats()->transfers);
    s_now += 1;
    (void)subghz_svc_step(&s_svc);
    TEST_ASSERT_EQUAL_UINT32(xfers + 4u + 3u, fake_cc1101_stats()->transfers);
}

static void test_the_meter_logs_a_level_only_when_it_moved_by_3_db(void)
{
    ready();
    fake_cc1101_air(433920000u, -40);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    run_for(120);
    TEST_ASSERT_EQUAL(1, count_logs(SUBGHZ_LOG_METER));
    TEST_ASSERT_EQUAL(1, count_logs(SUBGHZ_LOG_RSSI));
    fake_cc1101_air(433920000u, -41); /* 1 dB: shown, not logged */
    run_for(120);
    TEST_ASSERT_EQUAL(1, count_logs(SUBGHZ_LOG_RSSI));
    fake_cc1101_air(433920000u, -43); /* 3 dB from the last logged: logged */
    run_for(120);
    TEST_ASSERT_EQUAL(2, count_logs(SUBGHZ_LOG_RSSI));
    fake_cc1101_air(433920000u, -37); /* and upwards too */
    run_for(120);
    TEST_ASSERT_EQUAL(3, count_logs(SUBGHZ_LOG_RSSI));
    assert_log(1, SUBGHZ_LOG_METER, 433920000u, 0); /* after "cc1101 version" */
    assert_log(2, SUBGHZ_LOG_RSSI, (unsigned)(int)-80, 0);
}

static void test_retuning_the_meter_follows_the_new_frequency(void)
{
    ready();
    fake_cc1101_air(434000000u, -50);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    run_for(150);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_INT16_WITHIN(2, -220, st.dbm_x2); /* the floor: -110 dBm */
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 434000000u));
    status(&st);
    TEST_ASSERT_FALSE(st.valid); /* a new frequency starts a new reading */
    run_for(150);
    status(&st);
    TEST_ASSERT_EQUAL_UINT32(434000000u, st.hz);
    TEST_ASSERT_INT16_WITHIN(2, -100, st.dbm_x2);
    TEST_ASSERT_EQUAL(2, count_logs(SUBGHZ_LOG_METER));
    TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->out_of_band);
}

static void test_stop_returns_the_radio_to_idle_and_says_so(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    run_for(150);
    TEST_ASSERT_EQUAL(CC1101_MARC_RX, fake_cc1101_stats()->marcstate);
    subghz_svc_stop(&s_svc);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_IDLE, st.mode);
    TEST_ASSERT_FALSE(st.valid);
    run_for(SUBGHZ_IDLE_MS);
    assert_idle_radio();
    TEST_ASSERT_EQUAL(1, count_logs(SUBGHZ_LOG_IDLE));
    run_for(2 * SUBGHZ_IDLE_MS); /* said once, nothing more happens */
    TEST_ASSERT_EQUAL(1, count_logs(SUBGHZ_LOG_IDLE));
    subghz_svc_stop(&s_svc); /* a second stop with nothing running is silent */
    run_for(SUBGHZ_IDLE_MS);
    TEST_ASSERT_EQUAL(1, count_logs(SUBGHZ_LOG_IDLE));
    assert_idle_radio();
}

static void test_stop_before_the_first_step_still_ends_idle(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    subghz_svc_stop(&s_svc); /* the radio never left IDLE, but the order is honoured */
    run_for(SUBGHZ_IDLE_MS);
    assert_idle_radio();
    TEST_ASSERT_EQUAL(1, count_logs(SUBGHZ_LOG_IDLE));
    TEST_ASSERT_EQUAL(0, count_logs(SUBGHZ_LOG_RSSI));
}

static void test_a_start_cancels_a_pending_idle_message(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    subghz_svc_stop(&s_svc);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    run_for(SUBGHZ_IDLE_MS);
    TEST_ASSERT_EQUAL(0, count_logs(SUBGHZ_LOG_IDLE));
}

/* ---- scan ------------------------------------------------------------------ */

static void test_the_scan_finds_the_injected_carriers_channel(void)
{
    ready();
    fake_cc1101_air(433920000u, -40);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    uint32_t t0 = s_now;
    run_for(600);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_SCAN, st.mode);
    TEST_ASSERT_TRUE(st.scan_done);
    TEST_ASSERT_TRUE(st.valid);
    TEST_ASSERT_EQUAL_UINT8(100, st.percent);
    TEST_ASSERT_EQUAL_UINT32(433925000u, st.hz); /* the nearest channel above 433.920 */
    TEST_ASSERT_INT16_WITHIN(2, -80, st.dbm_x2);
    TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_SCAN_DONE));
    TEST_ASSERT_EQUAL(1, count_logs(SUBGHZ_LOG_PEAK));
    assert_log(s_nlog - 1, SUBGHZ_LOG_PEAK, 433925000u, (unsigned)(int)-80);
    TEST_ASSERT_TRUE(s_now - t0 >= 600);
    /* 69 channels, each tuned inside the band, none outside */
    const fake_cc1101_stats_t *fs = fake_cc1101_stats();
    TEST_ASSERT_EQUAL(0, fs->out_of_band);
    TEST_ASSERT_EQUAL_UINT32(433075000u, SUBGHZ_SCAN_FIRST_HZ);
    /* the radio is idle again once the sweep is over */
    assert_idle_radio();
}

static void test_a_sweep_takes_at_most_500_ms(void)
{
    ready();
    fake_cc1101_air(434700000u, -50);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    uint32_t t0 = s_now;
    stark_subghz_status_t st;
    do {
        uint32_t wait = subghz_svc_step(&s_svc);
        s_now += wait;
        status(&st);
        TEST_ASSERT_TRUE(s_now - t0 <= 2000); /* the loop itself is bounded */
    } while (!st.scan_done);
    /* the injected clock only advances by the settle delays and the step waits (1 ms each) */
    TEST_ASSERT_TRUE_MESSAGE(s_now - t0 <= 500, "one sweep within 500 ms");
    TEST_ASSERT_EQUAL_UINT32(434700000u, st.hz);
}

static void test_scan_progress_runs_from_0_to_100(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    stark_subghz_status_t st;
    uint8_t last = 0;
    for (int i = 0; i < 200; i++) {
        s_now += subghz_svc_step(&s_svc);
        status(&st);
        TEST_ASSERT_TRUE(st.percent >= last);
        last = st.percent;
        if (st.scan_done) {
            break;
        }
        TEST_ASSERT_FALSE(st.valid); /* no peak until the sweep is over */
        TEST_ASSERT_EQUAL(CC1101_MARC_RX, fake_cc1101_stats()->marcstate);
    }
    TEST_ASSERT_EQUAL_UINT8(100, last);
}

static void test_a_finished_scan_stays_until_stop_and_scan_again_restarts(void)
{
    ready();
    fake_cc1101_air(433100000u, -45);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    run_for(600);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_EQUAL_UINT32(433100000u, st.hz);
    run_for(SUBGHZ_IDLE_MS * 2); /* nothing more happens, the radio stays idle */
    assert_idle_radio();
    TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_SCAN_DONE));
    fake_cc1101_air(434700000u, -60);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    status(&st);
    TEST_ASSERT_FALSE(st.scan_done);
    TEST_ASSERT_EQUAL_UINT8(0, st.percent);
    run_for(600);
    status(&st);
    TEST_ASSERT_EQUAL_UINT32(434700000u, st.hz);
    subghz_svc_stop(&s_svc);
    run_for(SUBGHZ_IDLE_MS);
    TEST_ASSERT_EQUAL(1, count_logs(SUBGHZ_LOG_IDLE));
    assert_idle_radio();
}

static void test_a_scan_stopped_midway_leaves_the_radio_idle(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    for (int i = 0; i < 10; i++) {
        s_now += subghz_svc_step(&s_svc);
    }
    TEST_ASSERT_EQUAL(CC1101_MARC_RX, fake_cc1101_stats()->marcstate);
    subghz_svc_stop(&s_svc);
    run_for(SUBGHZ_IDLE_MS);
    assert_idle_radio();
    TEST_ASSERT_EQUAL(0, count_events(STARK_SUBGHZ_EVT_SCAN_DONE));
}

static void test_a_scan_replaces_a_running_meter(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    run_for(100);
    fake_cc1101_air(433500000u, -35);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    run_for(600);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_EQUAL_UINT32(433500000u, st.hz);
    assert_idle_radio();
    /* and the meter replaces a finished scan */
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433500000u));
    run_for(150);
    status(&st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_METER, st.mode);
    TEST_ASSERT_INT16_WITHIN(2, -70, st.dbm_x2);
}

/* ---- faults: bounded, fail closed, never left receiving ---------------------- */

#define HDR_RSSI (CC1101_HDR_READ | CC1101_HDR_BURST | CC1101_RSSI)

static void fail_transfers(uint8_t hdr, unsigned skip, unsigned count)
{
    s_fail_hdr = hdr;
    s_fail_hdr_skip = skip;
    s_fail_hdr_count = count;
}

static void test_one_failed_sample_is_retried(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    run_for(100);
    fail_transfers(HDR_RSSI, 0, 1);
    fake_cc1101_air(433920000u, -50);
    run_for(300);
    TEST_ASSERT_EQUAL(0, s_fail_hdr_count); /* it did fail once */
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_TRUE(st.valid);
    TEST_ASSERT_INT16_WITHIN(2, -100, st.dbm_x2);
}

static void test_two_failed_samples_in_a_row_drop_the_module(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    run_for(100);
    fail_transfers(HDR_RSSI, 0, UINT_MAX);
    run_for(300);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_mc));
    TEST_ASSERT_EQUAL(1, count_logs(SUBGHZ_LOG_ERROR));
    assert_log(s_nlog - 1, SUBGHZ_LOG_ERROR, (unsigned)STARK_ERR_IO, 0);
    TEST_ASSERT_EQUAL(s_opens, s_closes);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_IDLE, st.mode);
    fail_transfers(0, 0, 0);
    run_for(SUBGHZ_RETRY_MAX_MS + 100); /* identified afresh, with a reset: IDLE */
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    assert_idle_radio();
}

static void test_a_retune_that_fails_once_is_retried_and_twice_drops(void)
{
    ready();
    fail_transfers(CC1101_SRX, 0, 1);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    fake_cc1101_air(433920000u, -40);
    run_for(200);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_TRUE(st.valid);
    TEST_ASSERT_INT16_WITHIN(2, -80, st.dbm_x2);
    fail_transfers(CC1101_SRX, 0, UINT_MAX);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 434000000u));
    run_for(200);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
}

static void test_a_chip_that_will_not_reach_rx_is_dropped(void)
{
    ready();
    fake_cc1101_fault(FAKE_CC1101_STUCK, -1); /* MARCSTATE never changes: SRX never arrives */
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    unsigned guard = 0;
    while (subghz_svc_ready(&s_svc) && guard++ < 50) {
        run_for(10);
    }
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_mc));
    TEST_ASSERT_TRUE(count_logs(SUBGHZ_LOG_ERROR) >= 1);
    TEST_ASSERT_EQUAL(s_opens, s_closes);
}

static void test_a_sweep_that_fails_starts_again_and_then_finishes(void)
{
    ready();
    fake_cc1101_air(433500000u, -45);
    fail_transfers(CC1101_SRX, 20, 1); /* the 21st channel's retune fails once */
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    run_for(900);
    TEST_ASSERT_EQUAL(0, s_fail_hdr_count);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_TRUE(st.scan_done);
    TEST_ASSERT_EQUAL_UINT32(433500000u, st.hz);
    TEST_ASSERT_EQUAL(1, count_events(STARK_SUBGHZ_EVT_SCAN_DONE));
}

static void test_a_sweep_that_keeps_failing_drops_the_module(void)
{
    ready();
    fail_transfers(CC1101_SRX, 10, UINT_MAX);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    run_for(900);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL(0, count_events(STARK_SUBGHZ_EVT_SCAN_DONE));
}

static void test_a_read_that_fails_mid_sweep_is_a_failed_sweep(void)
{
    ready();
    fail_transfers(HDR_RSSI, 30, 1);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    run_for(900);
    TEST_ASSERT_EQUAL(0, s_fail_hdr_count);
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_TRUE(st.scan_done); /* restarted from the first channel, and finished */
}

static void test_a_chip_that_will_not_go_idle_after_a_sweep_is_dropped(void)
{
    ready();
    /* every tune strobes SIDLE once: the 70th SIDLE is the sweep's own return to idle */
    fail_transfers(CC1101_SIDLE, SUBGHZ_SCAN_CHANNELS, UINT_MAX);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    run_for(900);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL(0, count_events(STARK_SUBGHZ_EVT_SCAN_DONE));
    TEST_ASSERT_EQUAL(0, count_logs(SUBGHZ_LOG_PEAK));
}

static void test_a_chip_that_will_not_go_idle_after_stop_is_dropped(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    run_for(100);
    fail_transfers(CC1101_SIDLE, 0, UINT_MAX);
    subghz_svc_stop(&s_svc);
    run_for(600);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc)); /* fail closed: reset on the next claim */
    TEST_ASSERT_EQUAL(0, count_logs(SUBGHZ_LOG_IDLE));
    fail_transfers(0, 0, 0);
    run_for(SUBGHZ_RETRY_MAX_MS + 100);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    assert_idle_radio();
}

static void test_losing_the_module_while_measuring_clears_the_measurement(void)
{
    ready();
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 433920000u));
    run_for(100);
    fake_24c02_remove(A0);
    module_ticks(2);
    run_for(100);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    stark_subghz_status_t st;
    status(&st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_IDLE, st.mode);
    TEST_ASSERT_FALSE(st.valid);
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, subghz_svc_meter(&s_svc, 433920000u));
    /* back again: nothing is measured until the app asks */
    fit_radio();
    run_for(SUBGHZ_IDLE_MS);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    status(&st);
    TEST_ASSERT_EQUAL(STARK_SUBGHZ_IDLE, st.mode);
    assert_idle_radio();
}

static void test_a_receive_only_session_never_transmits_or_raises_power(void)
{
    ready();
    fake_cc1101_air(433920000u, -40);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_scan(&s_svc));
    run_for(600);
    TEST_ASSERT_EQUAL(STARK_OK, subghz_svc_meter(&s_svc, 434790000u));
    run_for(200);
    subghz_svc_stop(&s_svc);
    run_for(SUBGHZ_IDLE_MS);
    const fake_cc1101_stats_t *fs = fake_cc1101_stats();
    TEST_ASSERT_EQUAL(0, fs->tx_strobes);
    TEST_ASSERT_EQUAL_HEX8(0x60, fs->patable_max);
    TEST_ASSERT_EQUAL(0, fs->patable_other);
    TEST_ASSERT_EQUAL(0, fs->out_of_band);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_requests_need_a_ready_module);
    RUN_TEST(test_a_frequency_outside_the_band_is_refused_before_the_chip);
    RUN_TEST(test_the_meter_follows_the_air_within_one_db);
    RUN_TEST(test_the_meter_samples_every_50_ms_and_only_announces_changes);
    RUN_TEST(test_a_step_before_the_next_sample_is_due_waits_for_it);
    RUN_TEST(test_the_meter_logs_a_level_only_when_it_moved_by_3_db);
    RUN_TEST(test_retuning_the_meter_follows_the_new_frequency);
    RUN_TEST(test_stop_returns_the_radio_to_idle_and_says_so);
    RUN_TEST(test_stop_before_the_first_step_still_ends_idle);
    RUN_TEST(test_a_start_cancels_a_pending_idle_message);
    RUN_TEST(test_the_scan_finds_the_injected_carriers_channel);
    RUN_TEST(test_a_sweep_takes_at_most_500_ms);
    RUN_TEST(test_scan_progress_runs_from_0_to_100);
    RUN_TEST(test_a_finished_scan_stays_until_stop_and_scan_again_restarts);
    RUN_TEST(test_a_scan_stopped_midway_leaves_the_radio_idle);
    RUN_TEST(test_a_scan_replaces_a_running_meter);
    RUN_TEST(test_one_failed_sample_is_retried);
    RUN_TEST(test_two_failed_samples_in_a_row_drop_the_module);
    RUN_TEST(test_a_retune_that_fails_once_is_retried_and_twice_drops);
    RUN_TEST(test_a_chip_that_will_not_reach_rx_is_dropped);
    RUN_TEST(test_a_sweep_that_fails_starts_again_and_then_finishes);
    RUN_TEST(test_a_sweep_that_keeps_failing_drops_the_module);
    RUN_TEST(test_a_read_that_fails_mid_sweep_is_a_failed_sweep);
    RUN_TEST(test_a_chip_that_will_not_go_idle_after_a_sweep_is_dropped);
    RUN_TEST(test_a_chip_that_will_not_go_idle_after_stop_is_dropped);
    RUN_TEST(test_losing_the_module_while_measuring_clears_the_measurement);
    RUN_TEST(test_a_receive_only_session_never_transmits_or_raises_power);
    return UNITY_END();
}
