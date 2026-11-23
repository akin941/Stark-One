/*
 * test_cc1101_drv.c — the CC1101 driver against the fake CC1101 (TASKS.md
 * STARK-0601, ADR-0022): detection, reset, configuration, the band lock and the
 * power cap seen from the chip's side, and every call bounded under every fake
 * fault. Time is the test's: it moves only when the driver waits.
 */
#include <string.h>
#include "cc1101_drv.h"
#include "fake_cc1101.h"
#include "unity.h"

static uint32_t s_now;
static unsigned s_delays;
static unsigned s_xfers;
static unsigned s_fail_from; /* the 1-based transfer from which every transfer fails (0: never) */
static bool s_clock_frozen;
static bool s_corrupt_readback; /* flips a bit of the FREQ2..0 read-back */
static cc1101_t s_d;

static stark_err_t io_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    (void)ctx;
    s_xfers++;
    if (s_fail_from != 0 && s_xfers >= s_fail_from) {
        return STARK_ERR_IO;
    }
    stark_err_t err = fake_cc1101_xfer(tx, rx, len);
    if (s_corrupt_readback && tx[0] == (CC1101_HDR_READ | CC1101_HDR_BURST | CC1101_FREQ2)) {
        rx[1] ^= 0x01u;
    }
    return err;
}

static uint32_t io_now(void *ctx)
{
    (void)ctx;
    return s_now;
}

static void io_delay(void *ctx, uint32_t ms)
{
    (void)ctx;
    s_delays++;
    if (!s_clock_frozen) {
        s_now += ms;
    }
}

static const cc1101_io_t k_io = {io_xfer, io_now, io_delay, NULL};

void setUp(void)
{
    fake_cc1101_reset();
    s_now = 5000;
    s_delays = s_xfers = s_fail_from = 0;
    s_clock_frozen = false;
    s_corrupt_readback = false;
    cc1101_bind(&s_d, &k_io);
}
void tearDown(void)
{}

static void raw_strobe(uint8_t s)
{
    uint8_t rx;
    TEST_ASSERT_EQUAL(STARK_OK, fake_cc1101_xfer(&s, &rx, 1));
}

static void test_detect_both_versions(void)
{
    uint8_t p;
    uint8_t v;
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_detect(&s_d, &p, &v));
    TEST_ASSERT_EQUAL_HEX8(0x00, p);
    TEST_ASSERT_EQUAL_HEX8(0x14, v);
    fake_cc1101_chip(0x00, 0x04);
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_detect(&s_d, &p, &v));
    TEST_ASSERT_EQUAL_HEX8(0x04, v);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_d.status); /* the chip's status byte, kept */
}

static void test_detect_wrong_chip_says_what_answered(void)
{
    static const struct {
        uint8_t part, ver;
    } wrong[] = {{0x01, 0x14}, {0x00, 0x15}, {0x00, 0x00}, {0xFF, 0xFF}, {0x80, 0x04}};
    for (size_t i = 0; i < sizeof wrong / sizeof wrong[0]; i++) {
        fake_cc1101_chip(wrong[i].part, wrong[i].ver);
        uint8_t p = 0;
        uint8_t v = 0;
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, cc1101_detect(&s_d, &p, &v));
        TEST_ASSERT_EQUAL_HEX8(wrong[i].part, p);
        TEST_ASSERT_EQUAL_HEX8(wrong[i].ver, v);
    }
}

static void test_detect_with_no_chip_and_every_fault(void)
{
    uint8_t p = 0;
    uint8_t v = 0;
    fake_cc1101_present(false);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, cc1101_detect(&s_d, &p, &v)); /* MISO high: 0xFF 0xFF */
    TEST_ASSERT_EQUAL_HEX8(0xFF, p);
    fake_cc1101_present(true);
    fake_cc1101_fault(FAKE_CC1101_SPI_ERROR, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, cc1101_detect(&s_d, &p, &v));
    TEST_ASSERT_EQUAL_HEX8(0xFF, p);
    TEST_ASSERT_EQUAL_HEX8(0xFF, v);
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);
    s_xfers = 0;
    s_fail_from = 2; /* the second read fails: neither byte is reported */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, cc1101_detect(&s_d, &p, &v));
    TEST_ASSERT_EQUAL_HEX8(0xFF, p);
    s_fail_from = 0;
    fake_cc1101_fault(FAKE_CC1101_BIT_FLIP, 1); /* one flipped bit is a wrong chip, not a hang */
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, cc1101_detect(&s_d, &p, &v));
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_detect(&s_d, &p, &v)); /* and it recovers */
}

static void test_reset(void)
{
    raw_strobe(0x34); /* SRX: the chip is in RX */
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_reset(&s_d));
    uint8_t st;
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_marcstate(&s_d, &st));
    TEST_ASSERT_EQUAL_HEX8(0x01, st);
    TEST_ASSERT_EQUAL(0, s_delays); /* IDLE at the first poll: no waiting */
    s_d.configured = true;
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_reset(&s_d));
    TEST_ASSERT_FALSE(s_d.configured);
}

static void test_reset_is_bounded_under_every_fault(void)
{
    raw_strobe(0x34);
    fake_cc1101_fault(FAKE_CC1101_STUCK, -1); /* MARCSTATE never reaches IDLE */
    unsigned before = s_xfers;
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, cc1101_reset(&s_d));
    TEST_ASSERT_TRUE(s_now - 5000u <= CC1101_RESET_TIMEOUT_MS + 1u);
    TEST_ASSERT_TRUE(s_xfers - before <= 1u + CC1101_MAX_POLLS);
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_reset(&s_d)); /* recovers */

    fake_cc1101_present(false); /* MARCSTATE reads 0x1F */
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, cc1101_reset(&s_d));
    fake_cc1101_present(true);

    s_clock_frozen = true; /* a clock that never moves still ends after the poll limit */
    raw_strobe(0x34);
    fake_cc1101_fault(FAKE_CC1101_STUCK, -1);
    before = s_xfers;
    s_delays = 0;
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, cc1101_reset(&s_d));
    TEST_ASSERT_EQUAL(CC1101_MAX_POLLS, s_xfers - before - 1u);
    TEST_ASSERT_EQUAL(CC1101_MAX_POLLS, s_delays);
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);

    fake_cc1101_fault(FAKE_CC1101_SPI_ERROR, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, cc1101_reset(&s_d)); /* the strobe itself fails */
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);
    s_xfers = 0;
    s_fail_from = 2; /* the strobe goes through, the first MARCSTATE read fails */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, cc1101_reset(&s_d));
}

static void test_frequency_vectors_reach_the_chip(void)
{
    static const struct {
        uint32_t hz;
        uint8_t r[3];
    } v[] = {{433050000u, {0x10, 0xA7, 0xE0}},
             {433920000u, {0x10, 0xB0, 0x71}},
             {434790000u, {0x10, 0xB9, 0x02}}};
    for (size_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, v[i].hz, 4800));
        const fake_cc1101_stats_t *st = fake_cc1101_stats();
        TEST_ASSERT_EQUAL_HEX8(v[i].r[0], st->reg[CC1101_FREQ2]);
        TEST_ASSERT_EQUAL_HEX8(v[i].r[1], st->reg[CC1101_FREQ1]);
        TEST_ASSERT_EQUAL_HEX8(v[i].r[2], st->reg[CC1101_FREQ0]);
    }
}

static void test_configure_writes_each_preset_completely(void)
{
    for (int p = 0; p < CC1101_PRESET_COUNT; p++) {
        fake_cc1101_reset();
        TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, (cc1101_preset_t)p, 433420000u, 9600));
        TEST_ASSERT_TRUE(s_d.configured);
        const cc1101_reg_t *r;
        size_t n = cc1101_preset_regs((cc1101_preset_t)p, &r);
        const fake_cc1101_stats_t *st = fake_cc1101_stats();
        for (size_t i = 0; i < n; i++) {
            uint8_t want = r[i].value;
            if (r[i].addr == CC1101_FREQ2) {
                want = 0x10;
            } else if (r[i].addr == CC1101_FREQ1) {
                want = 0xAB;
            } else if (r[i].addr == CC1101_FREQ0) {
                want = 0x85;
            } else if (r[i].addr == CC1101_MDMCFG4) {
                want = 0x88; /* the preset's 0x80 filter bits, DRATE_E 8 */
            } else if (r[i].addr == CC1101_MDMCFG3) {
                want = 0x83;
            }
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(want, st->reg[r[i].addr], "a preset register");
        }
        TEST_ASSERT_EQUAL_HEX8(0x01, st->marcstate); /* left in IDLE */
    }
}

static void test_configure_never_tunes_outside_the_band_and_never_exceeds_the_pa_cap(void)
{
    for (uint32_t hz = 400000000u; hz <= 470000000u; hz += 70123u) {
        uint32_t before = fake_cc1101_stats()->transfers;
        stark_err_t err = cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, hz, 4800);
        if (hz < CC1101_HZ_MIN || hz > CC1101_HZ_MAX) {
            TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, err);
            TEST_ASSERT_EQUAL_MESSAGE(before, fake_cc1101_stats()->transfers, "nothing was sent");
        } else {
            TEST_ASSERT_EQUAL(STARK_OK, err);
        }
    }
    static const uint32_t edge[] = {433049999u, 434790001u, 0, 868000000u, 0xFFFFFFFFu};
    for (size_t i = 0; i < sizeof edge / sizeof edge[0]; i++) {
        uint32_t before = fake_cc1101_stats()->transfers;
        TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                          cc1101_configure(&s_d, CC1101_PRESET_OOK_TX, edge[i], 4800));
        TEST_ASSERT_EQUAL(before, fake_cc1101_stats()->transfers);
    }
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_TX, 433050000u, 4800));
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_TX, 434790000u, 4800));
    const fake_cc1101_stats_t *st = fake_cc1101_stats();
    TEST_ASSERT_TRUE(st->freq_writes > 0);
    TEST_ASSERT_EQUAL_MESSAGE(0, st->out_of_band, "no tuned word outside 433.05-434.79 MHz");
    const uint32_t *log;
    size_t n = fake_cc1101_freq_log(&log);
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(log[i] >= 433049000u && log[i] <= 434791000u);
    }
    TEST_ASSERT_TRUE(st->patable_writes > 0);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x60, st->patable_max, "PATABLE never above the cap");
    TEST_ASSERT_EQUAL_MESSAGE(0, st->patable_other, "only 0x00 and 0x60 are ever written");
    TEST_ASSERT_EQUAL_HEX8(0x00, st->patable[0]);
    TEST_ASSERT_EQUAL_HEX8(0x60, st->patable[1]);
}

static void test_configure_refuses_bad_rates_and_presets_before_any_transfer(void)
{
    unsigned before = s_xfers;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 599));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 50001));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      cc1101_configure(&s_d, CC1101_PRESET_COUNT, 433920000u, 4800));
    TEST_ASSERT_EQUAL(before, s_xfers);
    TEST_ASSERT_FALSE(s_d.configured);
}

static void test_configure_survives_a_fault_at_every_step_and_recovers(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800));
    unsigned total = s_xfers;
    TEST_ASSERT_TRUE(total > 40 && total < 60);
    for (unsigned at = 1; at <= total; at++) {
        fake_cc1101_reset();
        s_xfers = 0;
        s_fail_from = at;
        TEST_ASSERT_EQUAL_MESSAGE(STARK_ERR_IO,
                                  cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800),
                                  "a failing transfer fails the call");
        TEST_ASSERT_FALSE(s_d.configured);
        TEST_ASSERT_EQUAL_MESSAGE(at, s_xfers, "and stops there");
        s_fail_from = 0;
        s_xfers = 0;
        TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800));
        TEST_ASSERT_TRUE(s_d.configured);
    }
}

static void test_configure_under_the_fake_faults(void)
{
    fake_cc1101_fault(FAKE_CC1101_NO_ANSWER, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT,
                      cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800));
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);

    raw_strobe(0x34);
    fake_cc1101_fault(FAKE_CC1101_STUCK, -1); /* SIDLE never takes: bounded, not a hang */
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT,
                      cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800));
    TEST_ASSERT_TRUE(s_now - 5000u <= 2u * CC1101_IDLE_TIMEOUT_MS);
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800));

    /* a persistent bit flip also turns MARCSTATE into something that is never IDLE */
    fake_cc1101_fault(FAKE_CC1101_BIT_FLIP, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT,
                      cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800));
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);

    /* the frequency read-back differs from what was written */
    s_corrupt_readback = true;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800));
    TEST_ASSERT_FALSE(s_d.configured);
    s_corrupt_readback = false;
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800));
}

static void test_strobes(void)
{
    unsigned before = s_xfers;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, cc1101_strobe(&s_d, CC1101_SRX)); /* nothing configured */
    TEST_ASSERT_EQUAL(before, s_xfers);
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800));
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_strobe(&s_d, CC1101_SRX));
    uint8_t st;
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_marcstate(&s_d, &st));
    TEST_ASSERT_EQUAL_HEX8(0x0D, st);
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_strobe(&s_d, CC1101_SFRX));
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_strobe(&s_d, CC1101_SFTX));
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_strobe(&s_d, CC1101_SNOP));
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_strobe(&s_d, CC1101_SIDLE));
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_marcstate(&s_d, &st));
    TEST_ASSERT_EQUAL_HEX8(0x01, st);
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_strobe(&s_d, CC1101_SRES));
    TEST_ASSERT_FALSE(s_d.configured); /* a reset forgets the profile */
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, cc1101_strobe(&s_d, CC1101_SRX));
}

static void test_stx_is_never_a_generic_strobe(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_TX, 433920000u, 4800));
    unsigned before = s_xfers;
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, cc1101_strobe(&s_d, CC1101_STX));
    TEST_ASSERT_EQUAL(before, s_xfers);
    TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->tx_strobes);
    static const uint8_t other[] = {0x00, 0x31, 0x33, 0x3E, 0x3F, 0x80, 0xB4, 0xFF};
    for (size_t i = 0; i < sizeof other; i++) {
        TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, cc1101_strobe(&s_d, other[i]));
    }
    TEST_ASSERT_EQUAL(before, s_xfers);
}

/* ---- cc1101_tx_start (STARK-0605): the one way into TX ---------------------------------- */

static void write_burst_direct(uint8_t addr, const uint8_t *v, size_t n)
{
    uint8_t tx[9];
    uint8_t rx[9];
    tx[0] = (uint8_t)(addr | CC1101_HDR_BURST);
    memcpy(tx + 1, v, n);
    TEST_ASSERT_EQUAL(STARK_OK, fake_cc1101_xfer(tx, rx, 1 + n));
}

static void configure_tx(uint32_t hz)
{
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_TX, hz, 4800));
}

static void test_tx_start_reads_the_chip_back_and_enters_tx(void)
{
    configure_tx(433920000u);
    unsigned before = s_xfers;
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_tx_start(&s_d, 433920000u));
    TEST_ASSERT_EQUAL(4u, s_xfers - before); /* FREQ read, PATABLE read, STX, MARCSTATE */
    const fake_cc1101_stats_t *st = fake_cc1101_stats();
    TEST_ASSERT_EQUAL_HEX8(0x13, st->marcstate);
    TEST_ASSERT_EQUAL(1, st->tx_strobes);
    TEST_ASSERT_EQUAL_HEX8(0x2D, st->reg[CC1101_IOCFG0]); /* serial data in on GDO0 */
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_idle(&s_d));
    TEST_ASSERT_EQUAL_HEX8(0x01, fake_cc1101_stats()->marcstate);
    /* both band edges are transmittable */
    configure_tx(433050000u);
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_tx_start(&s_d, 433050000u));
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_idle(&s_d));
    configure_tx(434790000u);
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_tx_start(&s_d, 434790000u));
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_idle(&s_d));
    TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->out_of_band);
}

static void test_tx_start_refuses_before_any_transfer(void)
{
    configure_tx(433920000u);
    unsigned before = s_xfers;
    static const uint32_t edge[] = {433049999u, 434790001u, 0, 868000000u, 0xFFFFFFFFu};
    for (size_t i = 0; i < sizeof edge / sizeof edge[0]; i++) {
        TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, cc1101_tx_start(&s_d, edge[i]));
    }
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, cc1101_tx_start(&s_d, 433930000u)); /* not what was set */
    TEST_ASSERT_EQUAL(before, s_xfers);
    TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->tx_strobes);
    /* a receive profile is not a transmit profile */
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800));
    before = s_xfers;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, cc1101_tx_start(&s_d, 433920000u));
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_FSK2_RX, 433920000u, 4800));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, cc1101_tx_start(&s_d, 433920000u));
    /* nothing configured, or reset since */
    configure_tx(433920000u);
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_reset(&s_d));
    before = s_xfers;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, cc1101_tx_start(&s_d, 433920000u));
    TEST_ASSERT_EQUAL(before, s_xfers);
    TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->tx_strobes);
}

static void test_tx_start_does_not_transmit_on_a_chip_that_is_not_as_written(void)
{
    /* the frequency words differ (the chip was retuned out of the band behind our back) */
    configure_tx(433920000u);
    static const uint8_t k_433_0[3] = {0x10, 0xA7, 0x61}; /* 433.00 MHz: outside the band */
    write_burst_direct(CC1101_FREQ2, k_433_0, 3);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, cc1101_tx_start(&s_d, 433920000u));
    TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->tx_strobes);
    TEST_ASSERT_EQUAL_HEX8(0x01, fake_cc1101_stats()->marcstate);
    /* a flipped bit in the read-back of the frequency */
    configure_tx(433920000u);
    s_corrupt_readback = true;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, cc1101_tx_start(&s_d, 433920000u));
    s_corrupt_readback = false;
    TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->tx_strobes);
    /* the power table is no longer the capped one: one byte at a time */
    for (unsigned i = 0; i < CC1101_PATABLE_SIZE; i++) {
        configure_tx(433920000u);
        uint8_t pa[CC1101_PATABLE_SIZE];
        memcpy(pa, cc1101_patable(), sizeof pa);
        pa[i] = (uint8_t)(i == 1u ? 0x61u : 0xC6u); /* the power-on default, or one step over */
        write_burst_direct(CC1101_PATABLE, pa, sizeof pa);
        TEST_ASSERT_EQUAL_MESSAGE(STARK_ERR_IO, cc1101_tx_start(&s_d, 433920000u),
                                  "PATABLE differs");
        TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->tx_strobes);
    }
    configure_tx(433920000u); /* and the written table is accepted again */
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_tx_start(&s_d, 433920000u));
}

static void test_tx_start_is_bounded_and_fails_at_every_transfer(void)
{
    configure_tx(433920000u);
    unsigned before = s_xfers;
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_tx_start(&s_d, 433920000u));
    unsigned total = s_xfers - before;
    TEST_ASSERT_EQUAL(4u, total);
    for (unsigned at = 1; at <= total; at++) {
        fake_cc1101_reset();
        configure_tx(433920000u);
        uint32_t strobes = fake_cc1101_stats()->tx_strobes;
        s_xfers = 0;
        s_fail_from = at;
        TEST_ASSERT_EQUAL(STARK_ERR_IO, cc1101_tx_start(&s_d, 433920000u));
        TEST_ASSERT_EQUAL(at, s_xfers);
        /* STX was sent only when the transfers before it all worked */
        TEST_ASSERT_EQUAL(at > 3u ? strobes + 1u : strobes, fake_cc1101_stats()->tx_strobes + 0u);
        s_fail_from = 0;
        TEST_ASSERT_EQUAL(STARK_OK, cc1101_idle(&s_d));
        TEST_ASSERT_EQUAL_HEX8(0x01, fake_cc1101_stats()->marcstate);
    }
    /* STX that never takes: a bounded wait, then a timeout */
    fake_cc1101_reset();
    configure_tx(433920000u);
    s_now = 5000;
    fake_cc1101_fault(FAKE_CC1101_STUCK, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, cc1101_tx_start(&s_d, 433920000u));
    TEST_ASSERT_TRUE(s_now - 5000u <= 2u * CC1101_TX_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(1, fake_cc1101_stats()->tx_strobes);
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_idle(&s_d));
    /* a chip that stopped answering */
    configure_tx(433920000u);
    fake_cc1101_fault(FAKE_CC1101_NO_ANSWER, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, cc1101_tx_start(&s_d, 433920000u));
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);
}

static void test_rssi_and_marcstate(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800));
    fake_cc1101_air(433920000u, -60);
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_strobe(&s_d, CC1101_SRX));
    uint8_t raw = 0;
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_rssi_raw(&s_d, &raw));
    TEST_ASSERT_EQUAL_HEX8(28, raw);
    fake_cc1101_air(433920000u, -100);
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_rssi_raw(&s_d, &raw));
    TEST_ASSERT_EQUAL_HEX8(0xCC, raw);
    fake_cc1101_fault(FAKE_CC1101_SPI_ERROR, -1);
    uint8_t st = 0;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, cc1101_rssi_raw(&s_d, &raw));
    TEST_ASSERT_EQUAL_HEX8(0xFF, raw);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, cc1101_marcstate(&s_d, &st));
    TEST_ASSERT_EQUAL_HEX8(0xFF, st);
}

static void test_tune_rx_retunes_and_leaves_the_chip_receiving(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800));
    uint32_t writes = fake_cc1101_stats()->freq_writes;
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_tune_rx(&s_d, 434775000u));
    const fake_cc1101_stats_t *st = fake_cc1101_stats();
    TEST_ASSERT_EQUAL_HEX8(0x0D, st->marcstate);
    TEST_ASSERT_EQUAL(writes + 1, st->freq_writes); /* one burst for the three registers */
    TEST_ASSERT_UINT32_WITHIN(500, 434775000u, st->tuned_hz);
    fake_cc1101_air(434775000u, -55);
    uint8_t raw = 0;
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_rssi_raw(&s_d, &raw));
    TEST_ASSERT_EQUAL_HEX8(38, raw); /* (-55 + 74) * 2 */
    /* from RX to another channel: still receiving there */
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_tune_rx(&s_d, 433075000u));
    st = fake_cc1101_stats();
    TEST_ASSERT_EQUAL_HEX8(0x0D, st->marcstate);
    TEST_ASSERT_UINT32_WITHIN(500, 433075000u, st->tuned_hz);
    TEST_ASSERT_EQUAL(0, st->out_of_band);
}

static void test_tune_rx_refuses_before_any_transfer(void)
{
    unsigned before = s_xfers;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, cc1101_tune_rx(&s_d, 433049999u));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, cc1101_tune_rx(&s_d, 434790001u));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, cc1101_tune_rx(&s_d, 433920000u)); /* not configured */
    TEST_ASSERT_EQUAL(before, s_xfers);
    TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->freq_writes);
}

static void test_tune_rx_under_faults_is_bounded(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800));
    /* the chip never changes state: SIDLE from IDLE is fine, SRX never arrives */
    fake_cc1101_fault(FAKE_CC1101_STUCK, -1);
    s_now = 5000;
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, cc1101_tune_rx(&s_d, 434000000u));
    TEST_ASSERT_TRUE(s_now - 5000u <= 10u);
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_tune_rx(&s_d, 434000000u));
    /* from RX the SIDLE never arrives */
    fake_cc1101_fault(FAKE_CC1101_STUCK, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, cc1101_tune_rx(&s_d, 433500000u));
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, cc1101_idle(&s_d));
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);
    /* a transfer fails at each of the steps */
    for (unsigned n = 1; n <= 5; n++) {
        s_xfers = 0;
        s_fail_from = n;
        TEST_ASSERT_EQUAL(STARK_ERR_IO, cc1101_tune_rx(&s_d, 433500000u));
        s_fail_from = 0;
    }
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_tune_rx(&s_d, 433500000u));
}

static void test_idle(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800));
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_tune_rx(&s_d, 433920000u));
    TEST_ASSERT_EQUAL(STARK_OK, cc1101_idle(&s_d));
    TEST_ASSERT_EQUAL_HEX8(0x01, fake_cc1101_stats()->marcstate);
    s_fail_from = 1;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, cc1101_idle(&s_d));
}

static void test_every_call_is_bounded_under_every_fault(void)
{
    for (int f = FAKE_CC1101_NO_ANSWER; f < FAKE_CC1101_FAULT_COUNT; f++) {
        fake_cc1101_reset();
        raw_strobe(0x34);
        fake_cc1101_fault((fake_cc1101_fault_t)f, -1);
        s_now = 5000;
        s_xfers = 0;
        uint8_t a;
        uint8_t b;
        (void)cc1101_reset(&s_d);
        (void)cc1101_detect(&s_d, &a, &b);
        (void)cc1101_configure(&s_d, CC1101_PRESET_OOK_RX, 433920000u, 4800);
        (void)cc1101_strobe(&s_d, CC1101_SIDLE);
        (void)cc1101_rssi_raw(&s_d, &a);
        (void)cc1101_marcstate(&s_d, &a);
        (void)cc1101_tune_rx(&s_d, 433500000u);
        (void)cc1101_idle(&s_d);
        TEST_ASSERT_TRUE_MESSAGE(s_xfers < 200, "a bounded number of transfers");
        TEST_ASSERT_TRUE_MESSAGE(s_now - 5000u < 40u, "a bounded wait");
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_detect_both_versions);
    RUN_TEST(test_detect_wrong_chip_says_what_answered);
    RUN_TEST(test_detect_with_no_chip_and_every_fault);
    RUN_TEST(test_reset);
    RUN_TEST(test_reset_is_bounded_under_every_fault);
    RUN_TEST(test_frequency_vectors_reach_the_chip);
    RUN_TEST(test_configure_writes_each_preset_completely);
    RUN_TEST(test_configure_never_tunes_outside_the_band_and_never_exceeds_the_pa_cap);
    RUN_TEST(test_configure_refuses_bad_rates_and_presets_before_any_transfer);
    RUN_TEST(test_configure_survives_a_fault_at_every_step_and_recovers);
    RUN_TEST(test_configure_under_the_fake_faults);
    RUN_TEST(test_strobes);
    RUN_TEST(test_stx_is_never_a_generic_strobe);
    RUN_TEST(test_tx_start_reads_the_chip_back_and_enters_tx);
    RUN_TEST(test_tx_start_refuses_before_any_transfer);
    RUN_TEST(test_tx_start_does_not_transmit_on_a_chip_that_is_not_as_written);
    RUN_TEST(test_tx_start_is_bounded_and_fails_at_every_transfer);
    RUN_TEST(test_rssi_and_marcstate);
    RUN_TEST(test_tune_rx_retunes_and_leaves_the_chip_receiving);
    RUN_TEST(test_tune_rx_refuses_before_any_transfer);
    RUN_TEST(test_tune_rx_under_faults_is_bounded);
    RUN_TEST(test_idle);
    RUN_TEST(test_every_call_is_bounded_under_every_fault);
    return UNITY_END();
}
