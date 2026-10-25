/*
 * test_nfc_reader.c — finding and reading tags through the production
 * reader and PN532 driver against the fake PN532 (TASKS.md STARK-0403,
 * ADR-0020 §3), over the spec-derived tag catalogue
 * (test/fake/fake_tag_catalog.h).
 */
#include <string.h>
#include "fake_pn532.h"
#include "fake_tag_catalog.h"
#include "nfc_ident.h"
#include "nfc_reader.h"
#include "nfc_type2.h"
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
static stark_nfc_read_t s_out;
static stark_nfc_read_t s_again;

void setUp(void)
{
    fake_pn532_reset();
    pn532_bind(&s_d, &k_io);
    TEST_ASSERT_EQUAL(STARK_OK, pn532_field(&s_d, true));
}
void tearDown(void)
{}

static void place(const fake_catalog_entry_t *e)
{
    fake_pn532_tag(&e->tag);
    if (e->vanish_after >= 0) {
        fake_pn532_vanish_after((unsigned)e->vanish_after);
    }
}

static void read_tag(stark_nfc_read_t *out)
{
    bool found = false;
    nfc_tag_t tag;
    TEST_ASSERT_EQUAL(STARK_OK, nfc_reader_poll(&s_d, &found, &tag));
    TEST_ASSERT_TRUE(found);
    nfc_reader_read(&s_d, &tag, out);
}

static void assert_entry(const fake_catalog_entry_t *e, const stark_nfc_read_t *r)
{
    const char *l = e->label;
    TEST_ASSERT_EQUAL_MESSAGE(e->tag.uid_len, r->tag.uid_len, l);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(e->tag.uid, r->tag.uid, e->tag.uid_len, l);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(e->tag.atqa, r->tag.atqa, l);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(e->tag.sak, r->tag.sak, l);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(e->tech, nfc_tech_name(r->tag.tech), l);
    TEST_ASSERT_EQUAL_MESSAGE(e->read_err, r->read_err, l);
    TEST_ASSERT_EQUAL_MESSAGE(e->ndef_present, r->ndef_present, l);
    TEST_ASSERT_EQUAL_MESSAGE(e->writable, r->writable, l);
    TEST_ASSERT_EQUAL_MESSAGE(e->capacity ? nfc_t2_ndef_capacity(e->capacity) : 0, r->ndef_capacity,
                              l);
    TEST_ASSERT_EQUAL_MESSAGE(e->ndef_len, r->ndef_len, l);
    if (e->ndef_len > 0) {
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(e->ndef, r->ndef, e->ndef_len, l);
    }
    if (e->tag.type2 && e->capacity > 0) {
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, r->tag.pages, l);
    }
}

static void test_every_catalogue_tag(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(20, FAKE_CATALOG_COUNT);
    for (size_t i = 0; i < FAKE_CATALOG_COUNT; i++) {
        setUp();
        place(&k_fake_catalog[i]);
        read_tag(&s_out);
        assert_entry(&k_fake_catalog[i], &s_out);
    }
}

/* Exit 1's software half: 20 reads in a row, identical. */
static void test_twenty_consecutive_reads(void)
{
    const fake_catalog_entry_t *e = &k_fake_catalog[0];
    place(e);
    read_tag(&s_out);
    for (int i = 0; i < 20; i++) {
        read_tag(&s_again);
        TEST_ASSERT_EQUAL_MEMORY(&s_out, &s_again, sizeof s_out);
    }
    pn532_stats_t zero = {0};
    TEST_ASSERT_EQUAL_MEMORY(&zero, &s_d.stats, sizeof zero);
}

/* The tag leaves after each possible exchange: the identification stays, the
 * read says why it stopped, and nothing half-read is left. */
static void test_vanish_at_every_exchange(void)
{
    const fake_catalog_entry_t *e = &k_fake_catalog[12]; /* long-form NDEF: many reads */
    TEST_ASSERT_EQUAL_STRING("long-form NDEF length", e->label);
    bool completed = false;
    for (unsigned k = 0; k < 40 && !completed; k++) {
        setUp();
        fake_pn532_tag(&e->tag);
        fake_pn532_vanish_after(k);
        read_tag(&s_out);
        TEST_ASSERT_EQUAL_MEMORY(e->tag.uid, s_out.tag.uid, e->tag.uid_len);
        if (s_out.read_err == STARK_OK) {
            assert_entry(e, &s_out);
            completed = true;
            TEST_ASSERT_GREATER_THAN(3, k); /* version + CC + several data reads */
            continue;
        }
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, s_out.read_err);
        TEST_ASSERT_FALSE(s_out.ndef_present);
        TEST_ASSERT_EQUAL(0, s_out.ndef_len);
        uint8_t zero[NFC_NDEF_MAX] = {0};
        TEST_ASSERT_EQUAL_MEMORY(zero, s_out.ndef, sizeof zero);
    }
    TEST_ASSERT_TRUE(completed);
}

static void test_next_tag_has_no_residue(void)
{
    place(&k_fake_catalog[7]); /* two records */
    read_tag(&s_out);
    TEST_ASSERT_TRUE(s_out.ndef_present);
    place(&k_fake_catalog[2]); /* MIFARE Classic: identification only */
    read_tag(&s_out);
    assert_entry(&k_fake_catalog[2], &s_out);
    uint8_t zero[NFC_NDEF_MAX] = {0};
    TEST_ASSERT_EQUAL_MEMORY(zero, s_out.ndef, sizeof zero);
}

static void test_faults_mid_read(void)
{
    const fake_catalog_entry_t *e = &k_fake_catalog[7];
    place(e);
    fake_pn532_fault(FAKE_BAD_DCS, 2); /* two corrupt responses, each asked for again */
    read_tag(&s_out);
    assert_entry(e, &s_out);
    TEST_ASSERT_EQUAL(2, s_d.stats.naks_sent);
    fake_pn532_fault(FAKE_SEND_NACK, 1); /* a NACK: the command is repeated */
    read_tag(&s_out);
    assert_entry(e, &s_out);
    bool found = false;
    nfc_tag_t tag;
    TEST_ASSERT_EQUAL(STARK_OK, nfc_reader_poll(&s_d, &found, &tag));
    fake_pn532_fault(FAKE_NEVER_READY, 1); /* armed after the poll: GET_VERSION times out */
    nfc_reader_read(&s_d, &tag, &s_out);
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, s_out.read_err);
    TEST_ASSERT_FALSE(s_out.ndef_present);
    read_tag(&s_out); /* the next read is clean */
    assert_entry(e, &s_out);
}

static void test_poll_without_tag_or_field(void)
{
    bool found = true;
    nfc_tag_t tag;
    TEST_ASSERT_EQUAL(STARK_OK, nfc_reader_poll(&s_d, &found, &tag));
    TEST_ASSERT_FALSE(found);
    place(&k_fake_catalog[0]);
    TEST_ASSERT_EQUAL(STARK_OK, pn532_field(&s_d, false));
    TEST_ASSERT_EQUAL(STARK_OK, nfc_reader_poll(&s_d, &found, &tag));
    TEST_ASSERT_FALSE(found); /* no field, no tag */
    TEST_ASSERT_EQUAL(1, fake_pn532_stats()->list_field_off);
    fake_pn532_fault(FAKE_NO_DEVICE, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, nfc_reader_poll(&s_d, &found, &tag));
}

static void test_odd_uid_length_rejected(void)
{
    fake_tag_t t = k_fake_catalog[2].tag;
    t.uid_len = 5;
    fake_pn532_tag(&t);
    bool found = false;
    nfc_tag_t tag;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, nfc_reader_poll(&s_d, &found, &tag));
    TEST_ASSERT_FALSE(found);
}

static void test_reselect(void)
{
    place(&k_fake_catalog[0]);
    read_tag(&s_out);
    TEST_ASSERT_EQUAL(STARK_OK, nfc_reader_reselect(&s_d, &s_out.tag));
    place(&k_fake_catalog[3]);
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, nfc_reader_reselect(&s_d, &s_out.tag));
    fake_pn532_tag(NULL);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, nfc_reader_reselect(&s_d, &s_out.tag));
    fake_pn532_fault(FAKE_NO_DEVICE, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, nfc_reader_reselect(&s_d, &s_out.tag));
}

/* The tag is swapped between the poll and the read: the chip no longer has a
 * selected target, so the read stops at once and the identification falls
 * back to ATQA/SAK (no GET_VERSION). */
static void test_swap_between_poll_and_read(void)
{
    place(&k_fake_catalog[4]);
    bool found = false;
    nfc_tag_t tag;
    TEST_ASSERT_EQUAL(STARK_OK, nfc_reader_poll(&s_d, &found, &tag));
    place(&k_fake_catalog[0]);
    nfc_reader_read(&s_d, &tag, &s_out);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, s_out.read_err);
    TEST_ASSERT_EQUAL(NFC_TECH_ULTRALIGHT, s_out.tag.tech); /* ATQA 0044, SAK 00, no version */
    TEST_ASSERT_FALSE(s_out.ndef_present);
}

static void test_cc_larger_than_supported(void)
{
    fake_tag_t t = k_fake_catalog[0].tag;
    t.mem[14] = 0xFF; /* CC size byte: 2040 bytes */
    fake_pn532_tag(&t);
    read_tag(&s_out);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, s_out.read_err);
    TEST_ASSERT_FALSE(s_out.ndef_present);
}

static void test_exchange_rejects(void)
{
    uint8_t out[40];
    size_t got;
    static const uint8_t cmd[8] = {0};
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, nfc_reader_exchange(&s_d, cmd, 7, out, 4, &got));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, nfc_reader_exchange(&s_d, cmd, 2, out, 33, &got));
    /* no tag selected: the chip reports a target timeout */
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND,
                      nfc_reader_exchange(&s_d, (const uint8_t[]){0x30, 0x00}, 2, out, 16, &got));
    place(&k_fake_catalog[2]); /* Classic refuses an unauthenticated READ */
    bool found;
    nfc_tag_t tag;
    TEST_ASSERT_EQUAL(STARK_OK, nfc_reader_poll(&s_d, &found, &tag));
    TEST_ASSERT_EQUAL(STARK_ERR_IO,
                      nfc_reader_exchange(&s_d, (const uint8_t[]){0x30, 0x04}, 2, out, 16, &got));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_catalogue_tag);
    RUN_TEST(test_twenty_consecutive_reads);
    RUN_TEST(test_vanish_at_every_exchange);
    RUN_TEST(test_next_tag_has_no_residue);
    RUN_TEST(test_faults_mid_read);
    RUN_TEST(test_poll_without_tag_or_field);
    RUN_TEST(test_odd_uid_length_rejected);
    RUN_TEST(test_reselect);
    RUN_TEST(test_swap_between_poll_and_read);
    RUN_TEST(test_cc_larger_than_supported);
    RUN_TEST(test_exchange_rejects);
    return UNITY_END();
}
