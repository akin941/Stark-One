/*
 * test_fake_cc1101.c — the test-only CC1101 (TASKS.md STARK-0601, ADR-0022 §4)
 * against the datasheet's SPI protocol, byte by byte and without the driver: a
 * fake that is wrong would make every test built on it meaningless.
 */
#include <string.h>
#include "fake_cc1101.h"
#include "unity.h"

void setUp(void)
{
    fake_cc1101_reset();
}
void tearDown(void)
{}

/* One transfer; rx gets len bytes. */
static stark_err_t xfer(const uint8_t *tx, size_t len, uint8_t *rx)
{
    return fake_cc1101_xfer(tx, rx, len);
}

static uint8_t strobe(uint8_t s)
{
    uint8_t rx[1];
    TEST_ASSERT_EQUAL(STARK_OK, xfer(&s, 1, rx));
    return rx[0];
}

static uint8_t status_reg(uint8_t addr)
{
    uint8_t tx[2] = {(uint8_t)(0xC0u | addr), 0};
    uint8_t rx[2];
    TEST_ASSERT_EQUAL(STARK_OK, xfer(tx, 2, rx));
    return rx[1];
}

static void write_reg(uint8_t addr, uint8_t v)
{
    uint8_t tx[2] = {addr, v};
    uint8_t rx[2];
    TEST_ASSERT_EQUAL(STARK_OK, xfer(tx, 2, rx));
}

static uint8_t read_reg(uint8_t addr)
{
    uint8_t tx[2] = {(uint8_t)(0x80u | addr), 0};
    uint8_t rx[2];
    TEST_ASSERT_EQUAL(STARK_OK, xfer(tx, 2, rx));
    return rx[1];
}

static void test_status_byte_on_every_transfer(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, strobe(0x3D)); /* SNOP in IDLE: ready, state IDLE */
    uint8_t tx[2] = {0x02, 0x2E};
    uint8_t rx[2] = {9, 9};
    TEST_ASSERT_EQUAL(STARK_OK, xfer(tx, 2, rx));
    TEST_ASSERT_EQUAL_HEX8(0x00, rx[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, rx[1]);        /* a write clocks the status byte out again */
    strobe(0x34);                               /* SRX */
    TEST_ASSERT_EQUAL_HEX8(0x10, strobe(0x3D)); /* state RX in bits 6:4 */
    strobe(0x35);                               /* STX */
    TEST_ASSERT_EQUAL_HEX8(0x20, strobe(0x3D));
}

static void test_power_on_state_and_identity(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, status_reg(0x30)); /* PARTNUM */
    TEST_ASSERT_EQUAL_HEX8(0x14, status_reg(0x31)); /* VERSION */
    TEST_ASSERT_EQUAL_HEX8(0x01, status_reg(0x35)); /* MARCSTATE IDLE */
    TEST_ASSERT_EQUAL_HEX8(0x29, read_reg(0x00));   /* IOCFG2 default */
    TEST_ASSERT_EQUAL_HEX8(0xEC, read_reg(0x0F));   /* FREQ0 default */
    fake_cc1101_chip(0x00, 0x04);
    TEST_ASSERT_EQUAL_HEX8(0x04, status_reg(0x31));
    fake_cc1101_chip(0x55, 0x77);
    TEST_ASSERT_EQUAL_HEX8(0x55, status_reg(0x30));
}

static void test_status_registers_need_the_burst_bit(void)
{
    /* 0x30 without the burst bit is the SRES strobe, not PARTNUM (datasheet 10.4). */
    write_reg(0x0F, 0x12);
    uint8_t tx[2] = {0x80u | 0x30u, 0};
    uint8_t rx[2];
    TEST_ASSERT_EQUAL(STARK_OK, xfer(tx, 2, rx));
    TEST_ASSERT_EQUAL_HEX8(0xEC, read_reg(0x0F)); /* it reset the chip */
    TEST_ASSERT_EQUAL_HEX8(0x00, status_reg(0x30));
}

static void test_single_and_burst_register_access(void)
{
    write_reg(0x10, 0x87);
    TEST_ASSERT_EQUAL_HEX8(0x87, read_reg(0x10));
    uint8_t w[4] = {0x40u | 0x0Du, 0x10, 0xB0, 0x71};
    uint8_t rx[4];
    TEST_ASSERT_EQUAL(STARK_OK, xfer(w, 4, rx));
    uint8_t r[4] = {0xC0u | 0x0Du, 0, 0, 0};
    TEST_ASSERT_EQUAL(STARK_OK, xfer(r, 4, rx));
    TEST_ASSERT_EQUAL_HEX8(0x10, rx[1]);
    TEST_ASSERT_EQUAL_HEX8(0xB0, rx[2]);
    TEST_ASSERT_EQUAL_HEX8(0x71, rx[3]);
    TEST_ASSERT_EQUAL_HEX8(0xB0, read_reg(0x0E));
    /* a burst never runs off the end of the register file */
    uint8_t edge[4] = {0x40u | 0x2Eu, 1, 2, 3};
    TEST_ASSERT_EQUAL(STARK_OK, xfer(edge, 4, rx));
    TEST_ASSERT_EQUAL_HEX8(1, read_reg(0x2E));
    TEST_ASSERT_EQUAL_HEX8(0x31, read_reg(0x2D)); /* 0x2D untouched */
    uint8_t rd[4] = {0xC0u | 0x2Eu, 0, 0, 0};
    TEST_ASSERT_EQUAL(STARK_OK, xfer(rd, 4, rx));
    TEST_ASSERT_EQUAL_HEX8(1, rx[1]);
    TEST_ASSERT_EQUAL_HEX8(0, rx[2]);
}

static void test_patable_records_every_write(void)
{
    uint8_t w[9] = {0x40u | 0x3Eu, 0x00, 0x60, 0, 0, 0, 0, 0, 0};
    uint8_t rx[9];
    TEST_ASSERT_EQUAL(STARK_OK, xfer(w, 9, rx));
    const fake_cc1101_stats_t *st = fake_cc1101_stats();
    TEST_ASSERT_EQUAL(8, st->patable_writes);
    TEST_ASSERT_EQUAL_HEX8(0x60, st->patable_max);
    TEST_ASSERT_EQUAL(0, st->patable_other);
    TEST_ASSERT_EQUAL_HEX8(0x60, st->patable[1]);
    uint8_t r[9] = {0xC0u | 0x3Eu};
    TEST_ASSERT_EQUAL(STARK_OK, xfer(r, 9, rx));
    TEST_ASSERT_EQUAL_HEX8(0x60, rx[2]);
    uint8_t single[2] = {0x3E, 0xC6}; /* a single write goes to index 0: recorded as "other" */
    TEST_ASSERT_EQUAL(STARK_OK, xfer(single, 2, rx));
    st = fake_cc1101_stats();
    TEST_ASSERT_EQUAL(1, st->patable_other);
    TEST_ASSERT_EQUAL_HEX8(0xC6, st->patable_max);
}

static void test_strobes_drive_marcstate(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01, status_reg(0x35));
    strobe(0x34);
    TEST_ASSERT_EQUAL_HEX8(0x0D, status_reg(0x35)); /* RX */
    strobe(0x36);
    TEST_ASSERT_EQUAL_HEX8(0x01, status_reg(0x35)); /* IDLE */
    strobe(0x35);
    TEST_ASSERT_EQUAL_HEX8(0x13, status_reg(0x35)); /* TX */
    TEST_ASSERT_EQUAL(1, fake_cc1101_stats()->tx_strobes);
    strobe(0x3A);
    strobe(0x3B);
    strobe(0x3D);
    TEST_ASSERT_EQUAL_HEX8(0x13, status_reg(0x35)); /* those do not move the state */
    write_reg(0x0F, 0x00);
    strobe(0x30); /* SRES: back to IDLE with the datasheet defaults and PATABLE */
    TEST_ASSERT_EQUAL_HEX8(0x01, status_reg(0x35));
    TEST_ASSERT_EQUAL_HEX8(0xEC, read_reg(0x0F));
    TEST_ASSERT_EQUAL_HEX8(0xC6, fake_cc1101_stats()->patable[0]);
}

static void test_rssi_follows_the_air_at_the_tuned_frequency(void)
{
    uint8_t w[4] = {0x40u | 0x0Du, 0x10, 0xB0, 0x71}; /* 433.92 MHz */
    uint8_t rx[4];
    TEST_ASSERT_EQUAL(STARK_OK, xfer(w, 4, rx));
    fake_cc1101_air(433920000u, -60);
    TEST_ASSERT_EQUAL_HEX8(0x00, status_reg(0x34)); /* not in RX: nothing measured yet */
    strobe(0x34);
    TEST_ASSERT_EQUAL_HEX8(28, status_reg(0x34)); /* (-60 + 74) * 2 */
    fake_cc1101_air(433920000u, -100);
    TEST_ASSERT_EQUAL_HEX8(0xCC, status_reg(0x34)); /* (-26 * 2) + 256 */
    fake_cc1101_air(433920000u + 12000u, -80);      /* within half a scan channel */
    TEST_ASSERT_EQUAL_HEX8(0xF4, status_reg(0x34)); /* (-6 * 2) + 256 */
    fake_cc1101_air(433920000u - 12000u, -80);
    TEST_ASSERT_EQUAL_HEX8(0xF4, status_reg(0x34));
    fake_cc1101_air(433920000u + 13000u, -40);     /* outside it (STARK-0602: was 50 kHz) */
    TEST_ASSERT_EQUAL_HEX8(184, status_reg(0x34)); /* the -110 dBm floor */
    fake_cc1101_air(0, 0);
    TEST_ASSERT_EQUAL_HEX8(184, status_reg(0x34));
    fake_cc1101_air(433920000u, 10); /* beyond the chip's range: clamps */
    TEST_ASSERT_EQUAL_HEX8(127, status_reg(0x34));
    fake_cc1101_air(433920000u, -200);
    TEST_ASSERT_EQUAL_HEX8(128, status_reg(0x34));
    strobe(0x36);
    fake_cc1101_air(433920000u, -60);
    TEST_ASSERT_EQUAL_HEX8(128, status_reg(0x34)); /* idle: the last value stays */
}

static void test_faults(void)
{
    uint8_t rx[3];
    uint8_t tx[3] = {0xC0u | 0x30u, 0, 0};
    fake_cc1101_fault(FAKE_CC1101_NO_ANSWER, 1);
    TEST_ASSERT_EQUAL(STARK_OK, xfer(tx, 3, rx));
    TEST_ASSERT_EQUAL_HEX8(0xFF, rx[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, rx[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, status_reg(0x30)); /* the count ran out */

    fake_cc1101_fault(FAKE_CC1101_SPI_ERROR, 2);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, xfer(tx, 3, rx));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, xfer(tx, 3, rx));
    TEST_ASSERT_EQUAL(STARK_OK, xfer(tx, 3, rx));

    fake_cc1101_fault(FAKE_CC1101_BIT_FLIP, 1);
    TEST_ASSERT_EQUAL_HEX8(0x01, status_reg(0x30)); /* PARTNUM with bit 0 flipped */
    TEST_ASSERT_EQUAL_HEX8(0x00, status_reg(0x30));
    fake_cc1101_fault(FAKE_CC1101_BIT_FLIP, 1);
    TEST_ASSERT_EQUAL_HEX8(0x87 ^ 1, (write_reg(0x10, 0x87), read_reg(0x10)));
    fake_cc1101_fault(FAKE_CC1101_BIT_FLIP, 1);
    strobe(0x3D); /* a strobe has no data byte to flip: the fault waits */
    TEST_ASSERT_EQUAL_HEX8(0x01, status_reg(0x30));

    strobe(0x34); /* the state is RX; now it cannot change */
    fake_cc1101_fault(FAKE_CC1101_STUCK, -1);
    strobe(0x36);
    TEST_ASSERT_EQUAL_HEX8(0x0D, status_reg(0x35));
    strobe(0x30);
    TEST_ASSERT_EQUAL_HEX8(0x0D, status_reg(0x35)); /* even SRES leaves it where it was */
    strobe(0x35);
    TEST_ASSERT_EQUAL_HEX8(0x0D, status_reg(0x35));
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);
    strobe(0x36);
    TEST_ASSERT_EQUAL_HEX8(0x01, status_reg(0x35));

    fake_cc1101_fault(FAKE_CC1101_SPI_ERROR, -1); /* until cleared */
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(STARK_ERR_IO, xfer(tx, 3, rx));
    }
    fake_cc1101_fault(FAKE_CC1101_SPI_ERROR, 0);
    TEST_ASSERT_EQUAL(STARK_OK, xfer(tx, 3, rx));
}

static void test_absent_chip_and_bad_arguments(void)
{
    uint8_t rx[2];
    uint8_t tx[2] = {0xC0u | 0x31u, 0};
    fake_cc1101_present(false);
    TEST_ASSERT_EQUAL(STARK_OK, xfer(tx, 2, rx));
    TEST_ASSERT_EQUAL_HEX8(0xFF, rx[1]);
    fake_cc1101_present(true);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_cc1101_xfer(NULL, rx, 2));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_cc1101_xfer(tx, NULL, 2));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_cc1101_xfer(tx, rx, 0));
    uint8_t fifo[2] = {0x3F, 0}; /* the FIFO is not modelled: answered with the status byte */
    TEST_ASSERT_EQUAL(STARK_OK, xfer(fifo, 2, rx));
}

static void test_frequency_observation(void)
{
    uint8_t ok[4] = {0x40u | 0x0Du, 0x10, 0xB0, 0x71};
    uint8_t low[4] = {0x40u | 0x0Du, 0x10, 0xA7, 0xDF};
    uint8_t high[4] = {0x40u | 0x0Du, 0x10, 0xB9, 0x03};
    uint8_t rx[4];
    TEST_ASSERT_EQUAL(STARK_OK, xfer(ok, 4, rx));
    TEST_ASSERT_EQUAL(0, fake_cc1101_stats()->out_of_band);
    TEST_ASSERT_EQUAL(1, fake_cc1101_stats()->freq_writes);
    TEST_ASSERT_EQUAL(STARK_OK, xfer(low, 4, rx));
    TEST_ASSERT_EQUAL(STARK_OK, xfer(high, 4, rx));
    TEST_ASSERT_EQUAL(2, fake_cc1101_stats()->out_of_band);
    const uint32_t *log;
    TEST_ASSERT_EQUAL(3, fake_cc1101_freq_log(&log));
    TEST_ASSERT_TRUE(log[0] > 433919000u && log[0] < 433921000u);
    for (int i = 0; i < FAKE_CC1101_FREQ_LOG + 5; i++) {
        TEST_ASSERT_EQUAL(STARK_OK, xfer(ok, 4, rx));
    }
    TEST_ASSERT_EQUAL(FAKE_CC1101_FREQ_LOG, fake_cc1101_freq_log(&log)); /* the last ones */
}

static void test_scenes_on_gdo2_and_recording_on_gdo0(void)
{
    static const subghz_sym_t scene[] = {500, -500, 1000, -1000};
    subghz_sym_t out[8];
    fake_cc1101_scene(scene, 4);
    TEST_ASSERT_EQUAL(0, fake_cc1101_gdo2_take(out, 8)); /* not in RX */
    strobe(0x34);                                        /* entering RX arms it */
    TEST_ASSERT_EQUAL(2, fake_cc1101_gdo2_take(out, 2)); /* the caller's room limits a piece */
    TEST_ASSERT_EQUAL(500, out[0]);
    TEST_ASSERT_EQUAL(2, fake_cc1101_gdo2_take(out, 8)); /* the rest follows */
    TEST_ASSERT_EQUAL(1000, out[0]);
    TEST_ASSERT_EQUAL(0, fake_cc1101_gdo2_take(out, 8)); /* once */
    fake_cc1101_scene(scene, 4);
    TEST_ASSERT_EQUAL(4, fake_cc1101_gdo2_take(out, 8));
    TEST_ASSERT_EQUAL(500, out[0]);
    TEST_ASSERT_EQUAL(-1000, out[3]);
    fake_cc1101_scene(NULL, 0);
    TEST_ASSERT_EQUAL(0, fake_cc1101_gdo2_take(out, 8));
    strobe(0x36);
    strobe(0x34); /* re-entering RX with a scene in place arms it again */
    fake_cc1101_scene(scene, 4);
    strobe(0x36);
    TEST_ASSERT_EQUAL(0, fake_cc1101_gdo2_take(out, 8));
    strobe(0x34);
    TEST_ASSERT_EQUAL(4, fake_cc1101_gdo2_take(out, 8));

    const subghz_sym_t *rec;
    strobe(0x36);
    TEST_ASSERT_FALSE(fake_cc1101_gdo0_put(scene, 4)); /* not in TX: GDO0 is an input */
    strobe(0x35);
    TEST_ASSERT_TRUE(fake_cc1101_gdo0_put(scene, 4));
    TEST_ASSERT_TRUE(fake_cc1101_gdo0_put(scene, 2));
    TEST_ASSERT_EQUAL(6, fake_cc1101_gdo0_recorded(&rec));
    TEST_ASSERT_EQUAL(1000, rec[2]);
    static subghz_sym_t big[FAKE_CC1101_SCENE_MAX];
    TEST_ASSERT_FALSE(fake_cc1101_gdo0_put(big, FAKE_CC1101_SCENE_MAX)); /* would not fit */
    strobe(0x36);
    strobe(0x35); /* a new transmission starts a new recording */
    TEST_ASSERT_EQUAL(0, fake_cc1101_gdo0_recorded(&rec));
}

static void test_own_receiver_hears_the_recording(void)
{
    static const subghz_sym_t tx[] = {300, -300, 600};
    subghz_sym_t out[8];
    fake_cc1101_own_rx(true);
    strobe(0x35);
    TEST_ASSERT_TRUE(fake_cc1101_gdo0_put(tx, 3));
    TEST_ASSERT_TRUE(fake_cc1101_gdo0_put(tx, 3));
    strobe(0x36);
    strobe(0x34);
    TEST_ASSERT_EQUAL(6, fake_cc1101_gdo2_take(out, 8));
    TEST_ASSERT_EQUAL(600, out[2]);
    TEST_ASSERT_EQUAL(300, out[3]);
}

static void test_releasing_the_recording_leaves_no_dangling_scene(void)
{
    static const subghz_sym_t tx[] = {300, -300, 600};
    subghz_sym_t out[8];
    const subghz_sym_t *rec;
    fake_cc1101_own_rx(true);
    strobe(0x35);
    TEST_ASSERT_TRUE(fake_cc1101_gdo0_put(tx, 3));
    strobe(0x36);
    fake_cc1101_gdo0_release(); /* the own receiver was playing it: it plays nothing now */
    TEST_ASSERT_EQUAL(0, fake_cc1101_gdo0_recorded(&rec));
    TEST_ASSERT_NULL(rec);
    strobe(0x34);
    TEST_ASSERT_EQUAL(0, fake_cc1101_gdo2_take(out, 8));
    strobe(0x36);
    fake_cc1101_gdo0_release(); /* twice is harmless */
    strobe(0x35);               /* the next transmission records again */
    TEST_ASSERT_TRUE(fake_cc1101_gdo0_put(tx, 3));
    TEST_ASSERT_EQUAL(3, fake_cc1101_gdo0_recorded(&rec));
}

static void test_stats_and_counters(void)
{
    uint32_t before = fake_cc1101_stats()->transfers;
    strobe(0x3D);
    TEST_ASSERT_EQUAL(before + 1, fake_cc1101_stats()->transfers);
    TEST_ASSERT_EQUAL_HEX8(0x01, fake_cc1101_stats()->marcstate);
    TEST_ASSERT_EQUAL_HEX8(0x29, fake_cc1101_stats()->reg[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_status_byte_on_every_transfer);
    RUN_TEST(test_power_on_state_and_identity);
    RUN_TEST(test_status_registers_need_the_burst_bit);
    RUN_TEST(test_single_and_burst_register_access);
    RUN_TEST(test_patable_records_every_write);
    RUN_TEST(test_strobes_drive_marcstate);
    RUN_TEST(test_rssi_follows_the_air_at_the_tuned_frequency);
    RUN_TEST(test_faults);
    RUN_TEST(test_absent_chip_and_bad_arguments);
    RUN_TEST(test_frequency_observation);
    RUN_TEST(test_scenes_on_gdo2_and_recording_on_gdo0);
    RUN_TEST(test_own_receiver_hears_the_recording);
    RUN_TEST(test_releasing_the_recording_leaves_no_dangling_scene);
    RUN_TEST(test_stats_and_counters);
    return UNITY_END();
}
