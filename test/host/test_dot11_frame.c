/*
 * test_dot11_frame.c — IEEE 802.11 MAC dissection (TASKS.md STARK-1001,
 * ADR-0027) against spec-derived vectors: a beacon, a probe request/response,
 * a data frame, control frames (ACK/RTS), and a deauthentication frame
 * classified on receive. Plus the IE walker's bounds, truncation at every
 * byte (ASan guards over-reads), and the malformed-input paths. Receive-side
 * only — there is no frame builder to test (ADR-0027 §3).
 */
#include <stddef.h>
#include <string.h>

#include "dot11_frame.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

/* 00:11:22:33:44:55 — the BSSID used across the management vectors. */
static const uint8_t k_bssid[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

/* A beacon: broadcast DA, SSID "Stark", supported rates, DS channel 6, seq 256. */
static const uint8_t k_beacon[] = {
    0x80, 0x00,                                     /* FC: mgmt, beacon */
    0x00, 0x00,                                     /* duration */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* addr1 DA broadcast */
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55,             /* addr2 SA */
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55,             /* addr3 BSSID */
    0x00, 0x10,                                     /* seq ctrl -> seq 256 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* timestamp */
    0x64, 0x00,                                     /* beacon interval */
    0x01, 0x00,                                     /* capability */
    0x00, 0x05, 'S',  't',  'a',  'r',  'k',        /* SSID "Stark" */
    0x01, 0x04, 0x82, 0x84, 0x8b, 0x96,             /* supported rates */
    0x03, 0x01, 0x06,                               /* DS param: channel 6 */
};

/* A probe request: wildcard (hidden) SSID, supported rates, seq 512. */
static const uint8_t k_probe_req[] = {
    0x40, 0x00,                         /* FC: mgmt, probe request */
    0x00, 0x00,                         /* duration */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* addr1 broadcast */
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, /* addr2 SA */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* addr3 broadcast */
    0x00, 0x20,                         /* seq ctrl -> seq 512 */
    0x00, 0x00,                         /* SSID wildcard (len 0) */
    0x01, 0x04, 0x82, 0x84, 0x8b, 0x96, /* supported rates */
};

/* A data frame: FromDS set, Protected set, seq 3; body must be ignored. */
static const uint8_t k_data[] = {
    0x08, 0x42,                         /* FC: data; FromDS + Protected */
    0x00, 0x00,                         /* duration */
    0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, /* addr1 */
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, /* addr2 */
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, /* addr3 */
    0x30, 0x00,                         /* seq ctrl -> seq 3 */
    0xDE, 0xAD, 0xBE, 0xEF,             /* (encrypted) body — never parsed */
};

/* An ACK: one address, no sequence control. */
static const uint8_t k_ack[] = {
    0xD4, 0x00,                         /* FC: control, ACK */
    0x00, 0x00,                         /* duration */
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, /* addr1 RA */
};

/* An RTS: two addresses. */
static const uint8_t k_rts[] = {
    0xB4, 0x00,                         /* FC: control, RTS */
    0x00, 0x00,                         /* duration */
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, /* addr1 RA */
    0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, /* addr2 TA */
};

/* A deauthentication frame: classified and counted on receive only. */
static const uint8_t k_deauth[] = {
    0xC0, 0x00,                         /* FC: mgmt, deauth */
    0x00, 0x00,                         /* duration */
    0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, /* addr1 DA */
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, /* addr2 SA */
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, /* addr3 BSSID */
    0x00, 0x00,                         /* seq ctrl */
    0x07, 0x00,                         /* reason code — not surfaced by the parser */
};

static void test_beacon(void)
{
    dot11_frame_t f;
    TEST_ASSERT_EQUAL(STARK_OK, dot11_parse(k_beacon, sizeof k_beacon, &f));
    TEST_ASSERT_EQUAL(DOT11_TYPE_MGMT, f.type);
    TEST_ASSERT_EQUAL_HEX8(DOT11_MGMT_BEACON, f.subtype);
    TEST_ASSERT_FALSE(f.to_ds);
    TEST_ASSERT_FALSE(f.from_ds);
    TEST_ASSERT_FALSE(f.protected_frame);
    TEST_ASSERT_NOT_NULL(f.addr2);
    TEST_ASSERT_EQUAL_MEMORY(k_bssid, f.addr3, 6);
    TEST_ASSERT_TRUE(f.has_seq);
    TEST_ASSERT_EQUAL_UINT16(256, f.seq);
    TEST_ASSERT_TRUE(f.has_bss);
    TEST_ASSERT_EQUAL_UINT8(5, f.ssid_len);
    TEST_ASSERT_EQUAL_MEMORY("Stark", f.ssid, 5);
    TEST_ASSERT_TRUE(f.has_channel);
    TEST_ASSERT_EQUAL_UINT8(6, f.channel);
    TEST_ASSERT_EQUAL_UINT8(4, f.rates_len);
    TEST_ASSERT_EQUAL_HEX8(0x82, f.rates[0]);
}

static void test_probe_req(void)
{
    dot11_frame_t f;
    TEST_ASSERT_EQUAL(STARK_OK, dot11_parse(k_probe_req, sizeof k_probe_req, &f));
    TEST_ASSERT_EQUAL(DOT11_TYPE_MGMT, f.type);
    TEST_ASSERT_EQUAL_HEX8(DOT11_MGMT_PROBE_REQ, f.subtype);
    TEST_ASSERT_EQUAL_UINT16(512, f.seq);
    TEST_ASSERT_TRUE(f.has_bss);
    TEST_ASSERT_NULL(f.ssid); /* wildcard/hidden SSID */
    TEST_ASSERT_EQUAL_UINT8(0, f.ssid_len);
    TEST_ASSERT_FALSE(f.has_channel);
    TEST_ASSERT_EQUAL_UINT8(4, f.rates_len);
}

static void test_data(void)
{
    dot11_frame_t f;
    TEST_ASSERT_EQUAL(STARK_OK, dot11_parse(k_data, sizeof k_data, &f));
    TEST_ASSERT_EQUAL(DOT11_TYPE_DATA, f.type);
    TEST_ASSERT_TRUE(f.from_ds);
    TEST_ASSERT_FALSE(f.to_ds);
    TEST_ASSERT_TRUE(f.protected_frame);
    TEST_ASSERT_NOT_NULL(f.addr1);
    TEST_ASSERT_NOT_NULL(f.addr2);
    TEST_ASSERT_NOT_NULL(f.addr3);
    TEST_ASSERT_TRUE(f.has_seq);
    TEST_ASSERT_EQUAL_UINT16(3, f.seq);
    TEST_ASSERT_FALSE(f.has_bss); /* no tagged section is read for data frames */
}

static void test_control_frames(void)
{
    dot11_frame_t f;
    TEST_ASSERT_EQUAL(STARK_OK, dot11_parse(k_ack, sizeof k_ack, &f));
    TEST_ASSERT_EQUAL(DOT11_TYPE_CTRL, f.type);
    TEST_ASSERT_NOT_NULL(f.addr1);
    TEST_ASSERT_NULL(f.addr2);
    TEST_ASSERT_NULL(f.addr3);
    TEST_ASSERT_FALSE(f.has_seq);
    TEST_ASSERT_FALSE(f.has_bss);

    TEST_ASSERT_EQUAL(STARK_OK, dot11_parse(k_rts, sizeof k_rts, &f));
    TEST_ASSERT_EQUAL(DOT11_TYPE_CTRL, f.type);
    TEST_ASSERT_NOT_NULL(f.addr1);
    TEST_ASSERT_NOT_NULL(f.addr2); /* RTS carries a transmitter address */
}

static void test_deauth_classified_not_parsed(void)
{
    dot11_frame_t f;
    TEST_ASSERT_EQUAL(STARK_OK, dot11_parse(k_deauth, sizeof k_deauth, &f));
    TEST_ASSERT_EQUAL(DOT11_TYPE_MGMT, f.type);
    TEST_ASSERT_EQUAL_HEX8(DOT11_MGMT_DEAUTH, f.subtype); /* counted, never emitted */
    TEST_ASSERT_FALSE(f.has_bss);
    /* The struct exposes no reason code or body — the frame is classified, not harvested. */
}

static void test_ie_walker_bounds(void)
{
    static const uint8_t ies[] = {0x00, 0x02, 0xAA, 0xBB, 0x03, 0x01, 0x06};
    const uint8_t *val = NULL;
    uint8_t val_len = 0u;
    TEST_ASSERT_TRUE(dot11_find_ie(ies, sizeof ies, 0x00, &val, &val_len));
    TEST_ASSERT_EQUAL_UINT8(2, val_len);
    TEST_ASSERT_EQUAL_HEX8(0xAA, val[0]);
    TEST_ASSERT_TRUE(dot11_find_ie(ies, sizeof ies, 0x03, &val, &val_len));
    TEST_ASSERT_EQUAL_UINT8(1, val_len);
    TEST_ASSERT_EQUAL_HEX8(0x06, val[0]);
    TEST_ASSERT_FALSE(dot11_find_ie(ies, sizeof ies, 0x05, &val, &val_len));

    /* A declared length that runs past the section end: no match, no over-read. */
    static const uint8_t bad[] = {0x00, 0x05, 0xAA};
    TEST_ASSERT_FALSE(dot11_find_ie(bad, sizeof bad, 0x00, &val, &val_len));
}

static void test_malformed(void)
{
    dot11_frame_t f;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, dot11_parse(NULL, 10, &f));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, dot11_parse(k_beacon, sizeof k_beacon, NULL));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, dot11_parse(k_beacon, 3, &f));
    /* A management frame shorter than its 24-byte header is truncated. */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, dot11_parse(k_beacon, 20, &f));

    /* An SSID element longer than 32 octets is malformed. */
    static const uint8_t ssid_big[] = {
        0x80, 0x00, 0x00, 0x00,                         /* FC + duration */
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             /* addr1 */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,             /* addr2 */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,             /* addr3 */
        0x00, 0x00,                                     /* seq */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* timestamp */
        0x64, 0x00, 0x01, 0x00,                         /* interval + capability */
        0x00, 0x21,                                     /* SSID tag, len 33 (> 32) */
        0,    0,    0,    0,    0,    0,    0,    0,    0, 0, 0, 0, 0, 0, 0, 0, 0,
        0,    0,    0,    0,    0,    0,    0,    0,    0, 0, 0, 0, 0, 0, 0, 0,
    };
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, dot11_parse(ssid_big, sizeof ssid_big, &f));
}

/* Parse each vector truncated at every length; ASan/UBSan guard against any
 * over-read. The parser must return without crashing — OK or INVALID_ARG. */
static void parse_every_truncation(const uint8_t *buf, size_t len)
{
    dot11_frame_t f;
    for (size_t n = 0; n <= len; n++) {
        stark_err_t e = dot11_parse(buf, n, &f);
        TEST_ASSERT_TRUE(e == STARK_OK || e == STARK_ERR_INVALID_ARG);
    }
}

static void test_truncation_at_every_byte(void)
{
    parse_every_truncation(k_beacon, sizeof k_beacon);
    parse_every_truncation(k_probe_req, sizeof k_probe_req);
    parse_every_truncation(k_data, sizeof k_data);
    parse_every_truncation(k_ack, sizeof k_ack);
    parse_every_truncation(k_rts, sizeof k_rts);
    parse_every_truncation(k_deauth, sizeof k_deauth);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_beacon);
    RUN_TEST(test_probe_req);
    RUN_TEST(test_data);
    RUN_TEST(test_control_frames);
    RUN_TEST(test_deauth_classified_not_parsed);
    RUN_TEST(test_ie_walker_bounds);
    RUN_TEST(test_malformed);
    RUN_TEST(test_truncation_at_every_byte);
    return UNITY_END();
}
