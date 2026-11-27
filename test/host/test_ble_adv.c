/*
 * test_ble_adv.c — Bluetooth LE advertising-PDU dissection (TASKS.md
 * STARK-1002, ADR-0027) against spec-derived vectors: an ADV_IND with Flags,
 * a complete local name, a 16-bit service UUID and manufacturer data; an
 * ADV_DIRECT_IND (AdvA + TargetA, no AD structures); a SCAN_RSP with a
 * shortened name. Plus the AD walker's bounds, truncation at every byte (ASan
 * guards over-reads), and the malformed-input paths. Receive-side only — there
 * is no advertiser builder to test (ADR-0027 §3).
 */
#include <stddef.h>
#include <string.h>

#include "ble_adv.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static const uint8_t k_adva[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

/* ADV_IND (TxAdd random): Flags, name "Stark", service UUID 0x180F, mfg data. */
static const uint8_t k_adv_ind[] = {
    0x40, 0x19,                              /* header: ADV_IND, TxAdd; length 25 */
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55,      /* AdvA */
    0x02, 0x01, 0x06,                        /* Flags = 0x06 */
    0x06, 0x09, 'S',  't',  'a',  'r',  'k', /* Complete Local Name "Stark" */
    0x03, 0x03, 0x0F, 0x18,                  /* Complete 16-bit Service UUID 0x180F */
    0x04, 0xFF, 0x4C, 0x00, 0x01,            /* Manufacturer data: company 0x004C + 0x01 */
};

/* ADV_DIRECT_IND: AdvA + TargetA, no AD structures. */
static const uint8_t k_direct[] = {
    0x01, 0x0C,                         /* header: ADV_DIRECT_IND; length 12 */
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, /* AdvA */
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, /* TargetA */
};

/* SCAN_RSP with a shortened local name "St". */
static const uint8_t k_scan_rsp[] = {
    0x04, 0x0A,                         /* header: SCAN_RSP; length 10 */
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, /* AdvA */
    0x03, 0x08, 'S',  't',              /* Shortened Local Name "St" */
};

static void test_adv_ind(void)
{
    ble_adv_t a;
    TEST_ASSERT_EQUAL(STARK_OK, ble_adv_parse(k_adv_ind, sizeof k_adv_ind, &a));
    TEST_ASSERT_EQUAL_HEX8(BLE_PDU_ADV_IND, a.pdu_type);
    TEST_ASSERT_TRUE(a.tx_add);
    TEST_ASSERT_FALSE(a.rx_add);
    TEST_ASSERT_EQUAL_MEMORY(k_adva, a.adv_a, 6);
    TEST_ASSERT_NULL(a.target_a);
    TEST_ASSERT_TRUE(a.has_flags);
    TEST_ASSERT_EQUAL_HEX8(0x06, a.flags);
    TEST_ASSERT_TRUE(a.name_complete);
    TEST_ASSERT_EQUAL_UINT8(5, a.name_len);
    TEST_ASSERT_EQUAL_MEMORY("Stark", a.name, 5);
    TEST_ASSERT_TRUE(a.uuid16_complete);
    TEST_ASSERT_EQUAL_UINT8(2, a.uuid16_len);
    TEST_ASSERT_EQUAL_HEX8(0x0F, a.uuid16[0]);
    TEST_ASSERT_EQUAL_HEX8(0x18, a.uuid16[1]);
    TEST_ASSERT_EQUAL_UINT8(3, a.mfg_len);
    TEST_ASSERT_EQUAL_HEX8(0x4C, a.mfg[0]);
}

static void test_direct_ind(void)
{
    ble_adv_t a;
    TEST_ASSERT_EQUAL(STARK_OK, ble_adv_parse(k_direct, sizeof k_direct, &a));
    TEST_ASSERT_EQUAL_HEX8(BLE_PDU_ADV_DIRECT_IND, a.pdu_type);
    TEST_ASSERT_EQUAL_MEMORY(k_adva, a.adv_a, 6);
    TEST_ASSERT_NOT_NULL(a.target_a);
    TEST_ASSERT_EQUAL_HEX8(0xAA, a.target_a[0]);
    TEST_ASSERT_FALSE(a.has_flags); /* no AD structures in a directed advert */
    TEST_ASSERT_NULL(a.name);
}

static void test_scan_rsp_short_name(void)
{
    ble_adv_t a;
    TEST_ASSERT_EQUAL(STARK_OK, ble_adv_parse(k_scan_rsp, sizeof k_scan_rsp, &a));
    TEST_ASSERT_EQUAL_HEX8(BLE_PDU_SCAN_RSP, a.pdu_type);
    TEST_ASSERT_NOT_NULL(a.name);
    TEST_ASSERT_FALSE(a.name_complete); /* shortened */
    TEST_ASSERT_EQUAL_UINT8(2, a.name_len);
    TEST_ASSERT_EQUAL_MEMORY("St", a.name, 2);
}

static void test_ad_walker_bounds(void)
{
    static const uint8_t ad[] = {0x02, 0x01, 0x06, 0x03, 0x03, 0x0F, 0x18};
    const uint8_t *val = NULL;
    uint8_t val_len = 0u;
    TEST_ASSERT_TRUE(ble_adv_find_ad(ad, sizeof ad, BLE_AD_FLAGS, &val, &val_len));
    TEST_ASSERT_EQUAL_UINT8(1, val_len);
    TEST_ASSERT_EQUAL_HEX8(0x06, val[0]);
    TEST_ASSERT_TRUE(ble_adv_find_ad(ad, sizeof ad, BLE_AD_UUID16_COMPLETE, &val, &val_len));
    TEST_ASSERT_EQUAL_UINT8(2, val_len);
    TEST_ASSERT_FALSE(ble_adv_find_ad(ad, sizeof ad, BLE_AD_NAME_COMPLETE, &val, &val_len));

    /* A declared length that runs past the section end: no match, no over-read. */
    static const uint8_t bad[] = {0x05, 0x09, 'a'};
    TEST_ASSERT_FALSE(ble_adv_find_ad(bad, sizeof bad, BLE_AD_NAME_COMPLETE, &val, &val_len));
    /* A zero length ends the walk (padding / early termination). */
    static const uint8_t pad[] = {0x00, 0x02, 0x01, 0x06};
    TEST_ASSERT_FALSE(ble_adv_find_ad(pad, sizeof pad, BLE_AD_FLAGS, &val, &val_len));
}

static void test_malformed(void)
{
    ble_adv_t a;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ble_adv_parse(NULL, 4, &a));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ble_adv_parse(k_adv_ind, sizeof k_adv_ind, NULL));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ble_adv_parse(k_adv_ind, 1, &a));
    /* A header length that overruns the buffer. */
    static const uint8_t overrun[] = {0x00, 0x1F, 0x11, 0x22};
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ble_adv_parse(overrun, sizeof overrun, &a));
}

static void parse_every_truncation(const uint8_t *buf, size_t len)
{
    ble_adv_t a;
    for (size_t n = 0; n <= len; n++) {
        stark_err_t e = ble_adv_parse(buf, n, &a);
        TEST_ASSERT_TRUE(e == STARK_OK || e == STARK_ERR_INVALID_ARG);
    }
}

static void test_truncation_at_every_byte(void)
{
    parse_every_truncation(k_adv_ind, sizeof k_adv_ind);
    parse_every_truncation(k_direct, sizeof k_direct);
    parse_every_truncation(k_scan_rsp, sizeof k_scan_rsp);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_adv_ind);
    RUN_TEST(test_direct_ind);
    RUN_TEST(test_scan_rsp_short_name);
    RUN_TEST(test_ad_walker_bounds);
    RUN_TEST(test_malformed);
    RUN_TEST(test_truncation_at_every_byte);
    return UNITY_END();
}
