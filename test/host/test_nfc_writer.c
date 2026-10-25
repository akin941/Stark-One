/*
 * test_nfc_writer.c — writing NDEF to a Type 2 tag through the production
 * writer, reader and PN532 driver against the fake PN532 (TASKS.md
 * STARK-0404, ADR-0020 §5). The fake records every WRITE address, so the
 * tests also prove nothing outside the data area is ever written.
 */
#include <string.h>
#include "fake_pn532.h"
#include "fake_tag_catalog.h"
#include "ndef.h"
#include "nfc_reader.h"
#include "nfc_type2.h"
#include "nfc_writer.h"
#include "unity.h"

static uint32_t s_now;

static stark_err_t io_write(void *ctx, const uint8_t *buf, size_t len)
{
    (void)ctx;
    return fake_pn532_write(buf, len);
}

static stark_err_t io_read(void *ctx, uint8_t *buf, size_t len)
{
    (void)ctx;
    return fake_pn532_read(buf, len);
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

static const pn532_io_t k_io = {io_write, io_read, io_now, io_delay, NULL};
static pn532_t s_d;
static stark_nfc_read_t s_read;
static uint8_t s_msg[NFC_NDEF_MAX + 16];

#define BLANK_215  1 /* catalogue indexes */
#define READ_ONLY  3
#define CLASSIC_1K 2
#define BAD_MAGIC  15

void setUp(void)
{
    fake_pn532_reset();
    pn532_bind(&s_d, &k_io);
    TEST_ASSERT_EQUAL(STARK_OK, pn532_field(&s_d, true));
}

/* Every WRITE the fake saw stayed in the tag's data area. */
static void assert_writes_in_data_area(uint16_t data_bytes)
{
    const fake_pn532_stats_t *st = fake_pn532_stats();
    for (uint32_t i = 0; i < st->writes && i < FAKE_MAX_WRITES; i++) {
        TEST_ASSERT_GREATER_OR_EQUAL(NFC_T2_DATA_PAGE, st->write_page[i]);
        TEST_ASSERT_LESS_THAN(NFC_T2_DATA_PAGE + data_bytes / 4u, st->write_page[i]);
    }
}

void tearDown(void)
{
    const fake_tag_t *t = fake_pn532_current_tag();
    if (t != NULL && t->type2) {
        assert_writes_in_data_area((uint16_t)(t->mem[14] * 8u));
    }
}

static const fake_tag_t *place(size_t i)
{
    fake_pn532_tag(&k_fake_catalog[i].tag);
    return &k_fake_catalog[i].tag;
}

static void read_back(void)
{
    bool found = false;
    nfc_tag_t tag;
    TEST_ASSERT_EQUAL(STARK_OK, nfc_reader_poll(&s_d, &found, &tag));
    TEST_ASSERT_TRUE(found);
    nfc_reader_read(&s_d, &tag, &s_read);
    TEST_ASSERT_EQUAL(STARK_OK, s_read.read_err);
}

static nfc_tag_t tag_of(const fake_tag_t *t)
{
    nfc_tag_t tag = {0};
    memcpy(tag.uid, t->uid, t->uid_len);
    tag.uid_len = t->uid_len;
    return tag;
}

static size_t uri_message(const char *uri)
{
    uint8_t payload[128];
    ndef_record_t r;
    TEST_ASSERT_EQUAL(STARK_OK, ndef_uri_make(uri, payload, sizeof payload, &r));
    return ndef_encode(&r, 1, s_msg, sizeof s_msg);
}

static void test_uri_written_and_read_back(void)
{
    nfc_tag_t tag = tag_of(place(BLANK_215));
    size_t n = uri_message("https://example.com/stark");
    uint16_t pages = 0;
    TEST_ASSERT_EQUAL(STARK_OK, nfc_writer_write(&s_d, &tag, s_msg, n, &pages));
    TEST_ASSERT_EQUAL((2 + n + 1 + 3) / 4, pages);
    read_back();
    TEST_ASSERT_TRUE(s_read.ndef_present);
    TEST_ASSERT_EQUAL(n, s_read.ndef_len);
    TEST_ASSERT_EQUAL_MEMORY(s_msg, s_read.ndef, n);
    ndef_message_t m;
    char uri[64];
    TEST_ASSERT_EQUAL(STARK_OK, ndef_parse(s_read.ndef, s_read.ndef_len, &m));
    TEST_ASSERT_EQUAL(STARK_OK, ndef_uri_get(&m.rec[0], uri, sizeof uri));
    TEST_ASSERT_EQUAL_STRING("https://example.com/stark", uri);
}

static void test_text_over_an_existing_message(void)
{
    nfc_tag_t tag = tag_of(place(0)); /* NTAG213 holding a URI */
    uint8_t payload[64];
    ndef_record_t r;
    TEST_ASSERT_EQUAL(STARK_OK, ndef_text_make("en", "STARK ONE lab", payload, sizeof payload, &r));
    size_t n = ndef_encode(&r, 1, s_msg, sizeof s_msg);
    uint16_t pages = 0;
    TEST_ASSERT_EQUAL(STARK_OK, nfc_writer_write(&s_d, &tag, s_msg, n, &pages));
    read_back();
    TEST_ASSERT_EQUAL_MEMORY(s_msg, s_read.ndef, n);
}

/* The largest message the CC allows fits; one byte more does not. */
static void test_capacity_boundary(void)
{
    nfc_tag_t tag = tag_of(place(BLANK_215));
    uint16_t cap = nfc_t2_ndef_capacity(496); /* NTAG215: CC size 0x3E */
    memset(s_msg, 0xA5, sizeof s_msg);
    uint16_t pages = 0;
    TEST_ASSERT_EQUAL(STARK_OK, nfc_writer_write(&s_d, &tag, s_msg, cap, &pages));
    read_back();
    TEST_ASSERT_EQUAL(cap, s_read.ndef_len);
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, nfc_writer_write(&s_d, &tag, s_msg, cap + 1u, &pages));
    TEST_ASSERT_EQUAL(0, pages);
}

static void test_refusals_write_nothing(void)
{
    uint16_t pages = 0;
    size_t n = uri_message("https://example.org/lab");
    nfc_tag_t ro = tag_of(place(READ_ONLY));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, nfc_writer_write(&s_d, &ro, s_msg, n, &pages));
    nfc_tag_t bad = tag_of(place(BAD_MAGIC));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, nfc_writer_write(&s_d, &bad, s_msg, n, &pages));
    nfc_tag_t blank = tag_of(&k_fake_catalog[BLANK_215].tag);
    place(0); /* another tag in the field than the one confirmed */
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, nfc_writer_write(&s_d, &blank, s_msg, n, &pages));
    fake_pn532_tag(NULL);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, nfc_writer_write(&s_d, &blank, s_msg, n, &pages));
    nfc_tag_t classic = tag_of(place(CLASSIC_1K)); /* refuses the unauthenticated CC read */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, nfc_writer_write(&s_d, &classic, s_msg, n, &pages));
    TEST_ASSERT_EQUAL(0, fake_pn532_stats()->writes);
}

/* The tag leaves after each exchange: a partial write is reported, never done. */
static void test_vanish_after_each_page(void)
{
    nfc_tag_t tag = tag_of(&k_fake_catalog[BLANK_215].tag);
    size_t n = uri_message("https://example.com/a/longer/path/for/several/pages");
    bool completed = false;
    for (unsigned k = 0; k < 40 && !completed; k++) {
        setUp();
        place(BLANK_215);
        fake_pn532_vanish_after(k);
        uint16_t pages = 0;
        stark_err_t err = nfc_writer_write(&s_d, &tag, s_msg, n, &pages);
        if (err == STARK_OK) {
            completed = true;
            TEST_ASSERT_GREATER_THAN(4, k);
            continue;
        }
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, err);
        TEST_ASSERT_LESS_OR_EQUAL((2 + n + 1 + 3) / 4, pages);
    }
    TEST_ASSERT_TRUE(completed);
}

static void test_faults_mid_write(void)
{
    nfc_tag_t tag = tag_of(place(BLANK_215));
    size_t n = uri_message("https://example.com/stark");
    fake_pn532_fault(FAKE_BAD_DCS, 2);
    uint16_t pages = 0;
    TEST_ASSERT_EQUAL(STARK_OK, nfc_writer_write(&s_d, &tag, s_msg, n, &pages));
    read_back();
    TEST_ASSERT_EQUAL_MEMORY(s_msg, s_read.ndef, n);
    fake_pn532_fault(FAKE_BUS_ERROR, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, nfc_writer_write(&s_d, &tag, s_msg, n, &pages));
}

/* Lock bits the CC does not mention: the writes are ignored, the read-back
 * catches it. */
static void test_locked_pages_caught_by_read_back(void)
{
    fake_tag_t t = k_fake_catalog[BLANK_215].tag;
    t.ro_from = 6;
    fake_pn532_tag(&t);
    nfc_tag_t tag = tag_of(&t);
    size_t n = uri_message("https://example.com/stark");
    uint16_t pages = 0;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, nfc_writer_write(&s_d, &tag, s_msg, n, &pages));
}

/* The TLV page by page: header, message, Terminator, zero padding. */
static void tlv(const uint8_t *msg, size_t len, uint8_t *out, size_t n)
{
    for (size_t page = 0; page < n / 4; page++) {
        nfc_t2_tlv_page(msg, len, page, out + 4 * page);
    }
}

static void test_tlv_pages(void)
{
    uint8_t out[300];
    static const uint8_t msg[3] = {0xD0, 0x00, 0x00};
    TEST_ASSERT_EQUAL(8, nfc_t2_tlv_size(3)); /* 03 03 msg FE + pad */
    tlv(msg, 3, out, 8);
    static const uint8_t want[8] = {0x03, 0x03, 0xD0, 0x00, 0x00, 0xFE, 0x00, 0x00};
    TEST_ASSERT_EQUAL_MEMORY(want, out, 8);
    TEST_ASSERT_EQUAL(4, nfc_t2_tlv_size(0)); /* 03 00 FE 00 */
    tlv(NULL, 0, out, 4);
    static const uint8_t empty[4] = {0x03, 0x00, 0xFE, 0x00};
    TEST_ASSERT_EQUAL_MEMORY(empty, out, 4);
    static uint8_t big[260];
    memset(big, 0x5A, sizeof big);
    TEST_ASSERT_EQUAL(260, nfc_t2_tlv_size(255)); /* 4 + 255 + FE: whole pages already */
    tlv(big, 255, out, 260);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[3]);
    TEST_ASSERT_EQUAL_HEX8(0x5A, out[258]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, out[259]);
    TEST_ASSERT_EQUAL(256, nfc_t2_tlv_size(250)); /* 2 + 250 + 1 = 253 -> 256 */
    TEST_ASSERT_EQUAL(0, nfc_t2_tlv_size(0xFFFF));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_uri_written_and_read_back);
    RUN_TEST(test_text_over_an_existing_message);
    RUN_TEST(test_capacity_boundary);
    RUN_TEST(test_refusals_write_nothing);
    RUN_TEST(test_vanish_after_each_page);
    RUN_TEST(test_faults_mid_write);
    RUN_TEST(test_locked_pages_caught_by_read_back);
    RUN_TEST(test_tlv_pages);
    return UNITY_END();
}
