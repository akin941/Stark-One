/*
 * test_wifi_survey.c — Wi-Fi channel/RSSI survey and the capture ring (TASKS.md
 * STARK-1003, ADR-0027): per-channel aggregation, the bounded distinct-BSSID
 * table, the fixed oldest-overwritten ring, and the documented log-line
 * format/parse round-trip. Pure; frames are built directly (the dissector has
 * its own suite).
 */
#include <stdio.h>
#include <string.h>

#include "survey.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

/*
 * Parse a line produced by capture_format back into *r — the round-trip that
 * proves the format is unambiguous. This lives in the test, not in the core:
 * the firmware only writes capture lines to the card, it never reads them back,
 * so a parser is test scaffolding rather than production code.
 */
static stark_err_t parse_line(const char *line, wifi_capture_rec_t *r)
{
    unsigned int ch = 0, ty = 0, sty = 0, seq = 0;
    unsigned int b[6] = {0};
    int rssi = 0;
    int got = sscanf(line, "ch=%u t=%u/%u rssi=%d bssid=%2x:%2x:%2x:%2x:%2x:%2x seq=%u", &ch, &ty,
                     &sty, &rssi, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &seq);
    if (got != 11) {
        return STARK_ERR_INVALID_ARG;
    }
    if (ch > WIFI_SURVEY_CHANNELS || ty > 3u || sty > 15u || seq > 0xFFFFu || rssi < -128 ||
        rssi > 127) {
        return STARK_ERR_INVALID_ARG;
    }
    r->channel = (uint8_t)ch;
    r->type = (uint8_t)ty;
    r->subtype = (uint8_t)sty;
    r->rssi = (int8_t)rssi;
    for (size_t i = 0; i < 6; i++) {
        r->bssid[i] = (uint8_t)b[i];
    }
    r->seq = (uint16_t)seq;
    return STARK_OK;
}

static const uint8_t k_bssid_a[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t k_bssid_b[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

/* A beacon-like frame carrying a DS channel and a BSSID. */
static dot11_frame_t frame_on(uint8_t channel, const uint8_t *bssid)
{
    dot11_frame_t f;
    memset(&f, 0, sizeof f);
    f.type = DOT11_TYPE_MGMT;
    f.subtype = DOT11_MGMT_BEACON;
    f.has_channel = (channel != 0u);
    f.channel = channel;
    f.addr3 = bssid;
    return f;
}

static void test_channel_stats(void)
{
    wifi_survey_t s;
    survey_init(&s);
    dot11_frame_t f = frame_on(6, k_bssid_a);
    TEST_ASSERT_EQUAL(STARK_OK, survey_add(&s, &f, -40));
    TEST_ASSERT_EQUAL(STARK_OK, survey_add(&s, &f, -50));
    TEST_ASSERT_EQUAL(STARK_OK, survey_add(&s, &f, -45));
    const wifi_chan_stat_t *st = &s.chan[6 - 1];
    TEST_ASSERT_EQUAL_UINT32(3, st->frames);
    TEST_ASSERT_EQUAL_INT8(-50, st->rssi_min);
    TEST_ASSERT_EQUAL_INT8(-40, st->rssi_max);
    TEST_ASSERT_EQUAL_INT8(-45, st->rssi_last);
}

static void test_current_channel_fallback(void)
{
    wifi_survey_t s;
    survey_init(&s);
    /* No DS channel, no current channel: rejected, nothing written. */
    dot11_frame_t none = frame_on(0, k_bssid_a);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, survey_add(&s, &none, -30));
    TEST_ASSERT_EQUAL_UINT16(0, s.bssid_count);

    /* The radio hops to 11; a channel-less frame buckets there. */
    survey_set_channel(&s, 11);
    TEST_ASSERT_EQUAL(STARK_OK, survey_add(&s, &none, -30));
    TEST_ASSERT_EQUAL_UINT32(1, s.chan[11 - 1].frames);

    /* A frame advertising an out-of-band channel (36) falls back to current (11). */
    dot11_frame_t ch36 = frame_on(36, k_bssid_a);
    TEST_ASSERT_EQUAL(STARK_OK, survey_add(&s, &ch36, -33));
    TEST_ASSERT_EQUAL_UINT32(2, s.chan[11 - 1].frames);
}

static void test_distinct_bssids(void)
{
    wifi_survey_t s;
    survey_init(&s);
    dot11_frame_t fa = frame_on(6, k_bssid_a);
    dot11_frame_t fb = frame_on(6, k_bssid_b);
    survey_add(&s, &fa, -40);
    survey_add(&s, &fb, -41);
    survey_add(&s, &fa, -42); /* repeat A */
    TEST_ASSERT_EQUAL_UINT16(2, s.bssid_count);

    /* The table is bounded: more distinct BSSIDs than it holds do not overflow. */
    for (unsigned int i = 0; i < WIFI_SURVEY_MAX_BSSIDS + 8u; i++) {
        uint8_t id[6] = {0x10, 0x20, 0x30, 0x40, (uint8_t)(i >> 8), (uint8_t)i};
        dot11_frame_t f = frame_on(6, id);
        survey_add(&s, &f, -50);
    }
    TEST_ASSERT_EQUAL_UINT16(WIFI_SURVEY_MAX_BSSIDS, s.bssid_count);
}

static void test_capture_ring_overwrites_oldest(void)
{
    wifi_capture_t c;
    capture_init(&c);
    for (unsigned int i = 0; i < WIFI_CAPTURE_RING + 6u; i++) {
        wifi_capture_rec_t r;
        memset(&r, 0, sizeof r);
        r.channel = 6;
        r.seq = (uint16_t)i;
        capture_push(&c, &r);
    }
    TEST_ASSERT_EQUAL_UINT32(WIFI_CAPTURE_RING, capture_len(&c));
    TEST_ASSERT_EQUAL_UINT32(WIFI_CAPTURE_RING + 6u, c.count);
    /* With RING=64 and 70 pushes: slot i%64. The newest (seq 69) is at slot 5,
     * the oldest surviving (seq 6) at slot 6, and slot 0 holds seq 64. */
    TEST_ASSERT_EQUAL_UINT16(WIFI_CAPTURE_RING + 5u, c.rec[5].seq);
    TEST_ASSERT_EQUAL_UINT16(6, c.rec[6].seq);
    TEST_ASSERT_EQUAL_UINT16(WIFI_CAPTURE_RING, c.rec[0].seq);
}

static void test_log_line_round_trip(void)
{
    wifi_capture_rec_t r;
    memset(&r, 0, sizeof r);
    r.channel = 6;
    r.type = 0;
    r.subtype = 8;
    r.rssi = -42;
    memcpy(r.bssid, k_bssid_a, 6);
    r.seq = 256;

    char line[WIFI_CAPTURE_LINE];
    TEST_ASSERT_EQUAL(STARK_OK, capture_format(&r, line, sizeof line));
    TEST_ASSERT_EQUAL_STRING("ch=6 t=0/8 rssi=-42 bssid=00:11:22:33:44:55 seq=256", line);

    wifi_capture_rec_t back;
    memset(&back, 0, sizeof back);
    TEST_ASSERT_EQUAL(STARK_OK, parse_line(line, &back));
    TEST_ASSERT_EQUAL_MEMORY(&r, &back, sizeof r);

    /* A too-small buffer is refused, not truncated. */
    char tiny[8];
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, capture_format(&r, tiny, sizeof tiny));
    /* Garbage does not parse. */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, parse_line("not a log line", &back));
}

static void test_log_line_extremes(void)
{
    wifi_capture_rec_t r;
    memset(&r, 0, sizeof r);
    r.channel = 14;
    r.type = 3;
    r.subtype = 15;
    r.rssi = -128;
    memset(r.bssid, 0xFF, 6);
    r.seq = 0xFFFFu;

    char line[WIFI_CAPTURE_LINE];
    TEST_ASSERT_EQUAL(STARK_OK, capture_format(&r, line, sizeof line));
    wifi_capture_rec_t back;
    memset(&back, 0, sizeof back);
    TEST_ASSERT_EQUAL(STARK_OK, parse_line(line, &back));
    TEST_ASSERT_EQUAL_MEMORY(&r, &back, sizeof r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_channel_stats);
    RUN_TEST(test_current_channel_fallback);
    RUN_TEST(test_distinct_bssids);
    RUN_TEST(test_capture_ring_overwrites_oldest);
    RUN_TEST(test_log_line_round_trip);
    RUN_TEST(test_log_line_extremes);
    return UNITY_END();
}
