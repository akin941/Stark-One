/*
 * test_wifi_svc.c — the pure Wi-Fi analysis step (TASKS.md STARK-1004,
 * ADR-0027): one received frame is dissected and folded into the survey and
 * capture ring. Built from the dissector and survey cores; feeds a spec-derived
 * beacon and checks the aggregates. Pure, no I/O.
 */
#include <string.h>

#include "unity.h"
#include "wifi_svc.h"

void setUp(void)
{}
void tearDown(void)
{}

/* A beacon (SSID "Stark", DS channel 6, BSSID 00:11:22:33:44:55, seq 256). */
static const uint8_t k_beacon[] = {
    0x80, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x11, 0x22,
    0x33, 0x44, 0x55, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x01, 0x00, 0x00, 0x05, 'S',
    't',  'a',  'r',  'k',  0x01, 0x04, 0x82, 0x84, 0x8b, 0x96, 0x03, 0x01, 0x06,
};
static const uint8_t k_bssid[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

static void test_counts_a_beacon(void)
{
    wifi_svc_t s;
    wifi_svc_init(&s);
    /* The radio reports channel 11, but the beacon's DS channel 6 wins. */
    TEST_ASSERT_EQUAL(STARK_OK, wifi_svc_on_frame(&s, k_beacon, sizeof k_beacon, -42, 11));
    TEST_ASSERT_EQUAL_UINT32(1, s.survey.chan[6 - 1].frames);
    TEST_ASSERT_EQUAL_INT8(-42, s.survey.chan[6 - 1].rssi_last);
    TEST_ASSERT_EQUAL_UINT16(1, s.survey.bssid_count);
    TEST_ASSERT_EQUAL_UINT32(1, capture_len(&s.capture));
    TEST_ASSERT_EQUAL_UINT8(0x08, s.capture.rec[0].subtype);
    TEST_ASSERT_EQUAL_UINT16(256, s.capture.rec[0].seq);
    TEST_ASSERT_EQUAL_MEMORY(k_bssid, s.capture.rec[0].bssid, 6);
}

static void test_rejects_bad_input(void)
{
    wifi_svc_t s;
    wifi_svc_init(&s);
    /* Out-of-range channel. */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      wifi_svc_on_frame(&s, k_beacon, sizeof k_beacon, -40, 0));
    /* Malformed frame (too short for a management header). */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, wifi_svc_on_frame(&s, k_beacon, 10, -40, 6));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, wifi_svc_on_frame(&s, NULL, 0, -40, 6));
    /* Nothing was counted. */
    TEST_ASSERT_EQUAL_UINT32(0, capture_len(&s.capture));
    TEST_ASSERT_EQUAL_UINT32(0, s.survey.chan[6 - 1].frames);
}

static void test_data_frame_uses_radio_channel(void)
{
    wifi_svc_t s;
    wifi_svc_init(&s);
    /* A data frame carries no DS channel, so it buckets on the radio's channel. */
    static const uint8_t data[] = {
        0x08, 0x00, 0x00, 0x00, 0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x00, 0x11,
        0x22, 0x33, 0x44, 0x55, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x30, 0x00,
    };
    TEST_ASSERT_EQUAL(STARK_OK, wifi_svc_on_frame(&s, data, sizeof data, -60, 9));
    TEST_ASSERT_EQUAL_UINT32(1, s.survey.chan[9 - 1].frames);
    TEST_ASSERT_EQUAL_UINT32(0, s.survey.chan[6 - 1].frames);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_counts_a_beacon);
    RUN_TEST(test_rejects_bad_input);
    RUN_TEST(test_data_frame_uses_radio_channel);
    return UNITY_END();
}
