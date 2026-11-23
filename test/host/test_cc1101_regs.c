/*
 * test_cc1101_regs.c — the CC1101 register profiles (TASKS.md STARK-0601,
 * ADR-0022 §5). The frequency and data-rate vectors were computed in Python from
 * the datasheet formulas (round(f * 2^16 / 26e6), and
 * baud = 26e6 * (256 + M) * 2^E / 2^28 with the first E that puts 256 + M in
 * 256..511), independently of the firmware.
 */
#include <string.h>
#include "cc1101_regs.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static void test_freq_vectors(void)
{
    static const struct {
        uint32_t hz;
        uint8_t r[3];
    } v[] = {
        {433050000u, {0x10, 0xA7, 0xE0}}, /* the lower band edge */
        {433075000u, {0x10, 0xA8, 0x20}}, {433420000u, {0x10, 0xAB, 0x85}},
        {433500123u, {0x10, 0xAC, 0x4F}}, {433920000u, {0x10, 0xB0, 0x71}},
        {434000000u, {0x10, 0xB1, 0x3B}}, {434420000u, {0x10, 0xB5, 0x5E}},
        {434790000u, {0x10, 0xB9, 0x02}}, /* the upper band edge */
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        uint8_t out[3] = {0xEE, 0xEE, 0xEE};
        TEST_ASSERT_EQUAL(STARK_OK, cc1101_freq_regs(v[i].hz, out));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(v[i].r, out, 3);
    }
}

static void test_freq_outside_the_band_is_refused(void)
{
    static const uint32_t bad[] = {0,          1,          315000000u, 433049999u,
                                   434790001u, 868000000u, 915000000u, 0xFFFFFFFFu};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        uint8_t out[3] = {0xEE, 0xEE, 0xEE};
        TEST_ASSERT_EQUAL_MESSAGE(STARK_ERR_INVALID_ARG, cc1101_freq_regs(bad[i], out), "band");
        TEST_ASSERT_EQUAL_HEX8(0xEE, out[0]); /* nothing is written for a refused request */
    }
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, cc1101_freq_regs(433920000u, NULL));
}

static void test_every_in_band_frequency_encodes_inside_the_band(void)
{
    for (uint32_t hz = CC1101_HZ_MIN; hz <= CC1101_HZ_MAX; hz += 997u) {
        uint8_t r[3];
        TEST_ASSERT_EQUAL(STARK_OK, cc1101_freq_regs(hz, r));
        uint32_t word = ((uint32_t)r[0] << 16) | ((uint32_t)r[1] << 8) | r[2];
        TEST_ASSERT_TRUE(word >= 1091552u && word <= 1095938u);
    }
}

static void test_rate_vectors(void)
{
    static const struct {
        uint32_t baud;
        uint8_t e, m;
    } v[] = {
        {600, 4, 0x83},  {1200, 5, 0x83},  {2400, 6, 0x83},   {4800, 7, 0x83},
        {9600, 8, 0x83}, {10000, 8, 0x93}, {38400, 10, 0x83}, {50000, 10, 0xF8},
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        uint8_t e = 0xEE;
        uint8_t m = 0xEE;
        TEST_ASSERT_EQUAL(STARK_OK, cc1101_rate_regs(v[i].baud, &e, &m));
        TEST_ASSERT_EQUAL_HEX8(v[i].e, e);
        TEST_ASSERT_EQUAL_HEX8(v[i].m, m);
    }
}

static void test_rate_is_close_everywhere_and_refused_outside(void)
{
    for (uint32_t baud = CC1101_BAUD_MIN; baud <= CC1101_BAUD_MAX; baud += 37u) {
        uint8_t e;
        uint8_t m;
        TEST_ASSERT_EQUAL(STARK_OK, cc1101_rate_regs(baud, &e, &m));
        TEST_ASSERT_TRUE(e <= 15);
        double got = 26e6 * (256.0 + m) * (double)(1u << e) / 268435456.0;
        TEST_ASSERT_TRUE_MESSAGE(got > baud * 0.997 && got < baud * 1.003, "within 0.3 %");
    }
    uint8_t e = 0;
    uint8_t m = 0;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, cc1101_rate_regs(0, &e, &m));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, cc1101_rate_regs(599, &e, &m));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, cc1101_rate_regs(50001, &e, &m));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, cc1101_rate_regs(4800, NULL, &m));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, cc1101_rate_regs(4800, &e, NULL));
}

static uint8_t value_of(const cc1101_reg_t *r, size_t n, uint8_t addr, bool *found)
{
    for (size_t i = 0; i < n; i++) {
        if (r[i].addr == addr) {
            *found = true;
            return r[i].value;
        }
    }
    *found = false;
    return 0;
}

static void test_every_preset_is_complete_and_consistent(void)
{
    for (int p = 0; p < CC1101_PRESET_COUNT; p++) {
        const cc1101_reg_t *r = NULL;
        size_t n = cc1101_preset_regs((cc1101_preset_t)p, &r);
        TEST_ASSERT_EQUAL_MESSAGE(40, n, "a complete list");
        bool seen[CC1101_REG_LAST + 1] = {false};
        for (size_t i = 0; i < n; i++) {
            TEST_ASSERT_TRUE(r[i].addr <= CC1101_REG_LAST);
            TEST_ASSERT_FALSE_MESSAGE(seen[r[i].addr], "no register twice");
            seen[r[i].addr] = true;
        }
        bool f;
        TEST_ASSERT_EQUAL_HEX8(0x32, value_of(r, n, CC1101_PKTCTRL0, &f)); /* async serial */
        TEST_ASSERT_TRUE(f);
        TEST_ASSERT_EQUAL_HEX8(0x0D, value_of(r, n, CC1101_IOCFG2, &f)); /* GDO2: data out */
        TEST_ASSERT_TRUE(f);
        /* the defaults are 433.92 MHz at 4800 Bd with the 203 kHz channel filter */
        TEST_ASSERT_EQUAL_HEX8(0x10, value_of(r, n, CC1101_FREQ2, &f));
        TEST_ASSERT_EQUAL_HEX8(0xB0, value_of(r, n, CC1101_FREQ1, &f));
        TEST_ASSERT_EQUAL_HEX8(0x71, value_of(r, n, CC1101_FREQ0, &f));
        TEST_ASSERT_EQUAL_HEX8(0x87, value_of(r, n, CC1101_MDMCFG4, &f));
        TEST_ASSERT_EQUAL_HEX8(0x83, value_of(r, n, CC1101_MDMCFG3, &f));
    }
}

static void test_presets_differ_only_where_they_must(void)
{
    const cc1101_reg_t *rx;
    const cc1101_reg_t *tx;
    const cc1101_reg_t *fsk;
    size_t n = cc1101_preset_regs(CC1101_PRESET_OOK_RX, &rx);
    TEST_ASSERT_EQUAL(n, cc1101_preset_regs(CC1101_PRESET_OOK_TX, &tx));
    TEST_ASSERT_EQUAL(n, cc1101_preset_regs(CC1101_PRESET_FSK2_RX, &fsk));
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL(rx[i].addr, tx[i].addr); /* the same write order */
        TEST_ASSERT_EQUAL(rx[i].addr, fsk[i].addr);
        if (rx[i].addr == CC1101_IOCFG0) {
            TEST_ASSERT_EQUAL_HEX8(0x2E, rx[i].value); /* GDO0 high impedance while receiving */
            TEST_ASSERT_EQUAL_HEX8(0x2D, tx[i].value); /* GDO0 serial data in for TX */
            TEST_ASSERT_EQUAL_HEX8(0x2E, fsk[i].value);
        } else if (rx[i].addr == CC1101_MDMCFG2) {
            TEST_ASSERT_EQUAL_HEX8(0x30, rx[i].value); /* OOK */
            TEST_ASSERT_EQUAL_HEX8(0x30, tx[i].value);
            TEST_ASSERT_EQUAL_HEX8(0x00, fsk[i].value); /* 2-FSK */
        } else if (rx[i].addr == CC1101_FREND0) {
            TEST_ASSERT_EQUAL_HEX8(0x11, rx[i].value); /* OOK: PATABLE index 0 off, 1 on */
            TEST_ASSERT_EQUAL_HEX8(0x11, tx[i].value);
            TEST_ASSERT_EQUAL_HEX8(0x10, fsk[i].value);
        } else {
            TEST_ASSERT_EQUAL_HEX8(rx[i].value, tx[i].value);
            TEST_ASSERT_EQUAL_HEX8(rx[i].value, fsk[i].value);
        }
    }
}

static void test_unknown_preset_and_null_out(void)
{
    const cc1101_reg_t *r = (const cc1101_reg_t *)&r;
    TEST_ASSERT_EQUAL(0, cc1101_preset_regs(CC1101_PRESET_COUNT, &r));
    TEST_ASSERT_NULL(r);
    TEST_ASSERT_EQUAL(0, cc1101_preset_regs((cc1101_preset_t)99, NULL));
    TEST_ASSERT_EQUAL(40, cc1101_preset_regs(CC1101_PRESET_OOK_RX, NULL));
}

static void test_patable_image_never_exceeds_the_cap(void)
{
    const uint8_t *pa = cc1101_patable();
    TEST_ASSERT_EQUAL_HEX8(0x60, CC1101_PA_CAP);
    TEST_ASSERT_EQUAL_HEX8(0x00, pa[0]); /* OOK off */
    TEST_ASSERT_EQUAL_HEX8(0x60, pa[1]); /* OOK on */
    for (size_t i = 0; i < CC1101_PATABLE_SIZE; i++) {
        TEST_ASSERT_TRUE(pa[i] == 0x00 || pa[i] == CC1101_PA_CAP);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_freq_vectors);
    RUN_TEST(test_freq_outside_the_band_is_refused);
    RUN_TEST(test_every_in_band_frequency_encodes_inside_the_band);
    RUN_TEST(test_rate_vectors);
    RUN_TEST(test_rate_is_close_everywhere_and_refused_outside);
    RUN_TEST(test_every_preset_is_complete_and_consistent);
    RUN_TEST(test_presets_differ_only_where_they_must);
    RUN_TEST(test_unknown_preset_and_null_out);
    RUN_TEST(test_patable_image_never_exceeds_the_cap);
    return UNITY_END();
}
