/*
 * test_subghz_rssi.c — RSSI conversion, text and the band-scan state machine
 * (TASKS.md STARK-0602, ADR-0022): every raw byte 0..255 against the datasheet
 * formula written out again here, the scan's peak at each edge and in the
 * middle, ties, a flat floor, and a read or tune error mid-sweep. The scan runs
 * against an injected probe, so any sequence of readings can be played.
 */
#include <string.h>
#include "subghz_rssi.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

/* dBm * 2, from the datasheet text: (raw >= 128 ? (raw - 256) / 2 : raw / 2) - 74, doubled. */
static int expected_x2(unsigned raw)
{
    int half = raw >= 128u ? (int)raw - 256 : (int)raw; /* twice the first term */
    return half - 148;
}

static void test_every_raw_byte_converts_as_the_datasheet_says(void)
{
    for (unsigned raw = 0; raw < 256; raw++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(expected_x2(raw), subghz_rssi_dbm_x2((uint8_t)raw),
                                      "raw byte");
    }
    /* anchors worth reading by eye */
    TEST_ASSERT_EQUAL_INT16(-148, subghz_rssi_dbm_x2(0));   /* -74 dBm */
    TEST_ASSERT_EQUAL_INT16(-80, subghz_rssi_dbm_x2(68));   /* -40 dBm */
    TEST_ASSERT_EQUAL_INT16(-21, subghz_rssi_dbm_x2(127));  /* -10.5 dBm */
    TEST_ASSERT_EQUAL_INT16(-276, subghz_rssi_dbm_x2(128)); /* -138 dBm */
    TEST_ASSERT_EQUAL_INT16(-220, subghz_rssi_dbm_x2(184)); /* -110 dBm */
    TEST_ASSERT_EQUAL_INT16(-149, subghz_rssi_dbm_x2(255)); /* -74.5 dBm */
}

static void test_dbm_text(void)
{
    char t[16];
    subghz_dbm_text(t, sizeof t, -80);
    TEST_ASSERT_EQUAL_STRING("-40", t);
    subghz_dbm_text(t, sizeof t, -81);
    TEST_ASSERT_EQUAL_STRING("-40.5", t);
    subghz_dbm_text(t, sizeof t, 0);
    TEST_ASSERT_EQUAL_STRING("0", t);
    subghz_dbm_text(t, sizeof t, 1);
    TEST_ASSERT_EQUAL_STRING("0.5", t);
    subghz_dbm_text(t, sizeof t, -1);
    TEST_ASSERT_EQUAL_STRING("-0.5", t);
    subghz_dbm_text(t, sizeof t, -276);
    TEST_ASSERT_EQUAL_STRING("-138", t);
    subghz_dbm_text(t, sizeof t, INT16_MIN);
    TEST_ASSERT_EQUAL_STRING("-16384", t);
    subghz_dbm_text(t, 4, -81); /* truncated, still terminated */
    TEST_ASSERT_EQUAL_STRING("-40", t);
}

static void test_mhz_text(void)
{
    char t[16];
    subghz_mhz_text(t, sizeof t, 433925000u);
    TEST_ASSERT_EQUAL_STRING("433.925", t);
    subghz_mhz_text(t, sizeof t, 433050000u);
    TEST_ASSERT_EQUAL_STRING("433.050", t);
    subghz_mhz_text(t, sizeof t, 434790000u);
    TEST_ASSERT_EQUAL_STRING("434.790", t);
    subghz_mhz_text(t, sizeof t, 433920999u); /* kHz resolution: truncated */
    TEST_ASSERT_EQUAL_STRING("433.920", t);
}

/* ---- the scan against an injected probe ------------------------------------- */

static uint8_t s_level[SUBGHZ_SCAN_CHANNELS][SUBGHZ_SCAN_READS]; /* raw bytes per channel */
static unsigned s_chan;                                          /* the channel last tuned */
static unsigned s_reads_in;                                      /* reads since the last tune */
static unsigned s_tunes, s_reads, s_delays;
static uint32_t s_tuned[SUBGHZ_SCAN_CHANNELS + 2];
static unsigned s_order_errors;
static unsigned s_fail_tune_at; /* 1-based tune number that fails (0: never) */
static unsigned s_fail_read_at; /* 1-based read number that fails (0: never) */
static bool s_just_tuned;       /* tuned, and not yet settled */

static stark_err_t probe_tune(void *ctx, uint32_t hz)
{
    (void)ctx;
    s_tunes++;
    if (s_fail_tune_at != 0 && s_tunes == s_fail_tune_at) {
        return STARK_ERR_TIMEOUT;
    }
    if (s_tunes <= SUBGHZ_SCAN_CHANNELS + 1) {
        s_tuned[s_tunes - 1] = hz;
    }
    s_chan = (hz - SUBGHZ_SCAN_FIRST_HZ) / SUBGHZ_SCAN_STEP_HZ;
    s_reads_in = 0;
    s_just_tuned = true;
    return STARK_OK;
}

static stark_err_t probe_read(void *ctx, uint8_t *raw)
{
    (void)ctx;
    s_reads++;
    if (s_just_tuned) {
        s_order_errors++; /* a read before the settle delay */
    }
    if (s_fail_read_at != 0 && s_reads == s_fail_read_at) {
        return STARK_ERR_IO;
    }
    if (s_chan >= SUBGHZ_SCAN_CHANNELS || s_reads_in >= SUBGHZ_SCAN_READS) {
        s_order_errors++;
        *raw = 0;
        return STARK_OK;
    }
    *raw = s_level[s_chan][s_reads_in++];
    return STARK_OK;
}

static void probe_delay(void *ctx, uint32_t ms)
{
    (void)ctx;
    TEST_ASSERT_EQUAL_UINT32(SUBGHZ_SCAN_SETTLE_MS, ms);
    s_delays++;
    s_just_tuned = false;
}

static const subghz_probe_t k_probe = {probe_tune, probe_read, probe_delay, NULL};
static subghz_scan_t s_scan;

static void flat(uint8_t raw)
{
    memset(s_level, raw, sizeof s_level);
}

static void begin(void)
{
    s_tunes = s_reads = s_delays = s_order_errors = 0;
    s_fail_tune_at = s_fail_read_at = 0;
    s_just_tuned = false;
    subghz_scan_start(&s_scan, &k_probe);
}

static void run_to_end(void)
{
    unsigned guard = 0;
    while (subghz_scan_step(&s_scan) == SUBGHZ_SCAN_RUNNING) {
        TEST_ASSERT_TRUE(++guard < 100);
    }
}

static void test_the_band_geometry(void)
{
    TEST_ASSERT_EQUAL_UINT32(433075000u, SUBGHZ_SCAN_FIRST_HZ);
    TEST_ASSERT_EQUAL_UINT32(434775000u, SUBGHZ_SCAN_FIRST_HZ +
                                             (SUBGHZ_SCAN_CHANNELS - 1) * SUBGHZ_SCAN_STEP_HZ);
    TEST_ASSERT_EQUAL(69, SUBGHZ_SCAN_CHANNELS);
    TEST_ASSERT_EQUAL(4, SUBGHZ_SCAN_READS);
}

static void test_a_scan_visits_all_69_channels_in_order_and_settles_each(void)
{
    flat(184); /* -110 dBm */
    begin();
    TEST_ASSERT_EQUAL(SUBGHZ_SCAN_RUNNING, s_scan.state);
    TEST_ASSERT_EQUAL_UINT8(0, subghz_scan_percent(&s_scan));
    run_to_end();
    TEST_ASSERT_EQUAL(SUBGHZ_SCAN_DONE, s_scan.state);
    TEST_ASSERT_EQUAL(69, s_tunes);
    TEST_ASSERT_EQUAL(69, s_delays);
    TEST_ASSERT_EQUAL(69 * 4, s_reads);
    TEST_ASSERT_EQUAL(0, s_order_errors);
    for (unsigned i = 0; i < SUBGHZ_SCAN_CHANNELS; i++) {
        TEST_ASSERT_EQUAL_UINT32(433075000u + 25000u * i, s_tuned[i]);
    }
    TEST_ASSERT_EQUAL_UINT8(100, subghz_scan_percent(&s_scan));
    TEST_ASSERT_EQUAL_UINT32(433075000u, s_scan.peak_hz); /* a flat floor: the first channel */
    TEST_ASSERT_EQUAL_INT16(-220, s_scan.peak_dbm_x2);
}

static void test_the_peak_at_each_edge_and_in_the_middle(void)
{
    static const unsigned chans[] = {0, 1, 34, 67, 68};
    for (size_t i = 0; i < sizeof chans / sizeof chans[0]; i++) {
        flat(184);
        for (unsigned r = 0; r < SUBGHZ_SCAN_READS; r++) {
            s_level[chans[i]][r] = 68; /* -40 dBm on every read of that channel */
        }
        begin();
        run_to_end();
        TEST_ASSERT_EQUAL(SUBGHZ_SCAN_DONE, s_scan.state);
        TEST_ASSERT_EQUAL_UINT32(433075000u + 25000u * chans[i], s_scan.peak_hz);
        TEST_ASSERT_EQUAL_INT16(-80, s_scan.peak_dbm_x2);
    }
}

static void test_a_channel_takes_the_maximum_of_its_four_reads(void)
{
    flat(184);
    s_level[10][0] = 120; /* a single strong read in the first slot */
    s_level[11][3] = 100; /* a weaker one in the last slot */
    s_level[12][1] = 90;
    s_level[12][2] = 230; /* a negative raw byte: -87 dBm, weaker than any positive one */
    begin();
    run_to_end();
    TEST_ASSERT_EQUAL_UINT32(433075000u + 25000u * 10u, s_scan.peak_hz);
    TEST_ASSERT_EQUAL_INT16(subghz_rssi_dbm_x2(120), s_scan.peak_dbm_x2);
}

static void test_ties_go_to_the_lowest_channel(void)
{
    flat(184);
    for (unsigned r = 0; r < SUBGHZ_SCAN_READS; r++) {
        s_level[20][r] = 60;
        s_level[21][r] = 60;
        s_level[50][r] = 60;
    }
    begin();
    run_to_end();
    TEST_ASSERT_EQUAL_UINT32(433075000u + 25000u * 20u, s_scan.peak_hz);
    TEST_ASSERT_EQUAL_INT16(-88, s_scan.peak_dbm_x2);
}

static void test_a_positive_raw_byte_beats_every_negative_one(void)
{
    flat(128); /* -138 dBm: the weakest the byte can say */
    for (unsigned r = 0; r < SUBGHZ_SCAN_READS; r++) {
        s_level[5][r] = 200; /* -102 dBm */
        s_level[60][r] = 3;  /* -72.5 dBm */
    }
    begin();
    run_to_end();
    TEST_ASSERT_EQUAL_UINT32(433075000u + 25000u * 60u, s_scan.peak_hz);
    TEST_ASSERT_EQUAL_INT16(-145, s_scan.peak_dbm_x2);
}

static void test_progress_grows_with_each_channel(void)
{
    flat(184);
    begin();
    unsigned last = 0;
    for (unsigned i = 1; i <= SUBGHZ_SCAN_CHANNELS; i++) {
        subghz_scan_state_t st = subghz_scan_step(&s_scan);
        unsigned pct = subghz_scan_percent(&s_scan);
        TEST_ASSERT_TRUE(pct >= last);
        last = pct;
        TEST_ASSERT_EQUAL(i == SUBGHZ_SCAN_CHANNELS ? SUBGHZ_SCAN_DONE : SUBGHZ_SCAN_RUNNING, st);
    }
    TEST_ASSERT_EQUAL(100, last);
}

static void test_a_finished_scan_does_nothing_more(void)
{
    flat(184);
    begin();
    run_to_end();
    unsigned tunes = s_tunes;
    TEST_ASSERT_EQUAL(SUBGHZ_SCAN_DONE, subghz_scan_step(&s_scan));
    TEST_ASSERT_EQUAL(tunes, s_tunes);
}

static void test_a_tune_error_ends_the_sweep(void)
{
    flat(184);
    begin();
    s_fail_tune_at = 5;
    run_to_end();
    TEST_ASSERT_EQUAL(SUBGHZ_SCAN_FAILED, s_scan.state);
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, s_scan.err);
    TEST_ASSERT_EQUAL(5, s_tunes);
    TEST_ASSERT_EQUAL(SUBGHZ_SCAN_FAILED, subghz_scan_step(&s_scan)); /* and stays failed */
    TEST_ASSERT_EQUAL(5, s_tunes);
    TEST_ASSERT_EQUAL_UINT8(4 * 100 / 69, subghz_scan_percent(&s_scan));
}

static void test_a_read_error_mid_sweep_ends_the_sweep_at_any_read(void)
{
    for (unsigned at = 1; at <= 12; at++) { /* the 1st..4th read of channels 0..2 */
        flat(184);
        begin();
        s_fail_read_at = at;
        run_to_end();
        TEST_ASSERT_EQUAL(SUBGHZ_SCAN_FAILED, s_scan.state);
        TEST_ASSERT_EQUAL(STARK_ERR_IO, s_scan.err);
        TEST_ASSERT_EQUAL(at, s_reads);
    }
}

static void test_restarting_forgets_the_old_peak_and_error(void)
{
    flat(184);
    for (unsigned r = 0; r < SUBGHZ_SCAN_READS; r++) {
        s_level[40][r] = 68;
    }
    begin();
    s_fail_read_at = 200;
    run_to_end();
    TEST_ASSERT_EQUAL(SUBGHZ_SCAN_FAILED, s_scan.state);
    flat(184);
    begin(); /* the new sweep starts clean */
    TEST_ASSERT_EQUAL(STARK_OK, s_scan.err);
    run_to_end();
    TEST_ASSERT_EQUAL_UINT32(433075000u, s_scan.peak_hz);
    TEST_ASSERT_EQUAL_INT16(-220, s_scan.peak_dbm_x2);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_raw_byte_converts_as_the_datasheet_says);
    RUN_TEST(test_dbm_text);
    RUN_TEST(test_mhz_text);
    RUN_TEST(test_the_band_geometry);
    RUN_TEST(test_a_scan_visits_all_69_channels_in_order_and_settles_each);
    RUN_TEST(test_the_peak_at_each_edge_and_in_the_middle);
    RUN_TEST(test_a_channel_takes_the_maximum_of_its_four_reads);
    RUN_TEST(test_ties_go_to_the_lowest_channel);
    RUN_TEST(test_a_positive_raw_byte_beats_every_negative_one);
    RUN_TEST(test_progress_grows_with_each_channel);
    RUN_TEST(test_a_finished_scan_does_nothing_more);
    RUN_TEST(test_a_tune_error_ends_the_sweep);
    RUN_TEST(test_a_read_error_mid_sweep_ends_the_sweep_at_any_read);
    RUN_TEST(test_restarting_forgets_the_old_peak_and_error);
    return UNITY_END();
}
