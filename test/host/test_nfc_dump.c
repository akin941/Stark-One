/*
 * test_nfc_dump.c — tag dumps (TASKS.md STARK-0405): every catalogue tag
 * dumped through the production reader and driver against the fake PN532,
 * formatted, parsed back losslessly; the NDEF storage round trip (dump ->
 * text -> dump -> TLV -> NDEF gives the same records); malformed dumps,
 * fuzzing and file names.
 */
#include <stdio.h>
#include <string.h>
#include "fake_pn532.h"
#include "fake_tag_catalog.h"
#include "nfc_dump.h"
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
static stark_nfc_read_t s_read;
static nfc_dump_t s_dump, s_back;
static char s_text[8192];

void setUp(void)
{
    fake_pn532_reset();
    pn532_bind(&s_d, &k_io);
    TEST_ASSERT_EQUAL(STARK_OK, pn532_field(&s_d, true));
}
void tearDown(void)
{}

/* The whole text, streamed through a small buffer as the app does. */
static size_t format_all(const nfc_dump_t *d, size_t chunk)
{
    size_t cursor = 0;
    size_t total = 0;
    char buf[256];
    for (;;) {
        size_t n = nfc_dump_format(d, &cursor, buf, chunk);
        if (n == 0) {
            break;
        }
        TEST_ASSERT_LESS_THAN(sizeof s_text, total + n);
        memcpy(s_text + total, buf, n);
        total += n;
    }
    s_text[total] = '\0';
    return total;
}

static void dump_entry(size_t i)
{
    const fake_catalog_entry_t *e = &k_fake_catalog[i];
    setUp();
    fake_pn532_tag(&e->tag);
    bool found = false;
    nfc_tag_t tag;
    TEST_ASSERT_EQUAL(STARK_OK, nfc_reader_poll(&s_d, &found, &tag));
    nfc_reader_read(&s_d, &tag, &s_read); /* the snapshot the app dumps from */
    TEST_ASSERT_EQUAL_MESSAGE(STARK_OK, nfc_reader_dump(&s_d, &s_read.tag, &s_dump), e->label);
}

static void test_round_trip_every_catalogue_tag(void)
{
    for (size_t i = 0; i < FAKE_CATALOG_COUNT; i++) {
        const fake_catalog_entry_t *e = &k_fake_catalog[i];
        if (e->vanish_after >= 0) {
            continue; /* covered below */
        }
        dump_entry(i);
        if (e->tag.type2 && s_read.tag.pages > 0) {
            TEST_ASSERT_EQUAL_MESSAGE(s_read.tag.pages, s_dump.n_pages, e->label);
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(e->tag.mem, s_dump.mem, s_dump.n_pages * 4u, e->label);
        } else {
            TEST_ASSERT_EQUAL_MESSAGE(0, s_dump.n_pages, e->label); /* identification only */
        }
        size_t n = format_all(&s_dump, 64);
        TEST_ASSERT_EQUAL_MESSAGE(STARK_OK, nfc_dump_parse(s_text, n, &s_back), e->label);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_dump.tag.uid, s_back.tag.uid, s_dump.tag.uid_len,
                                         e->label);
        TEST_ASSERT_EQUAL_MESSAGE(s_dump.tag.uid_len, s_back.tag.uid_len, e->label);
        TEST_ASSERT_EQUAL_MESSAGE(s_dump.tag.atqa, s_back.tag.atqa, e->label);
        TEST_ASSERT_EQUAL_MESSAGE(s_dump.tag.sak, s_back.tag.sak, e->label);
        TEST_ASSERT_EQUAL_MESSAGE(s_dump.tag.tech, s_back.tag.tech, e->label);
        TEST_ASSERT_EQUAL_MESSAGE(s_dump.n_pages, s_back.n_pages, e->label);
        if (s_dump.n_pages > 0) {
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_dump.mem, s_back.mem, s_dump.n_pages * 4u, e->label);
        }
    }
}

/* Storage round trip: the NDEF message survives dump -> text -> dump. */
static void test_ndef_survives_the_file(void)
{
    for (size_t i = 0; i < FAKE_CATALOG_COUNT; i++) {
        const fake_catalog_entry_t *e = &k_fake_catalog[i];
        if (!e->ndef_present || e->vanish_after >= 0 || e->read_err != STARK_OK) {
            continue;
        }
        dump_entry(i);
        size_t n = format_all(&s_dump, 32); /* the smallest chunk: one line at a time */
        TEST_ASSERT_EQUAL(STARK_OK, nfc_dump_parse(s_text, n, &s_back));
        nfc_t2_cc_t cc = nfc_t2_cc(s_back.mem + 12);
        TEST_ASSERT_TRUE_MESSAGE(cc.valid, e->label);
        size_t off = 0, len = 0, need = 0;
        TEST_ASSERT_EQUAL_MESSAGE(
            NFC_TLV_FOUND,
            nfc_t2_tlv(s_back.mem + 16, cc.data_bytes, cc.data_bytes, &off, &len, &need), e->label);
        TEST_ASSERT_EQUAL_MESSAGE(e->ndef_len, len, e->label);
        if (len > 0) {
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(e->ndef, s_back.mem + 16 + off, len, e->label);
        }
    }
}

static void test_format_text(void)
{
    dump_entry(2); /* MIFARE Classic 1K: identification only */
    format_all(&s_dump, 256);
    TEST_ASSERT_EQUAL_STRING("stark-nfc 1\nuid DEADBEEF\natqa 0004\nsak 08\ntech CLASSIC_1K\n"
                             "pages 0\n",
                             s_text);
    dump_entry(0);
    format_all(&s_dump, 256);
    TEST_ASSERT_NOT_NULL(strstr(s_text, "\npage 0 04303B87\n")); /* UID0-2 + BCC0 */
    TEST_ASSERT_NOT_NULL(strstr(s_text, "\npage 44 00000000\n"));
    size_t cursor = 0;
    char small[16];
    TEST_ASSERT_EQUAL(0, nfc_dump_format(&s_dump, &cursor, small, sizeof small)); /* cap */
    TEST_ASSERT_EQUAL(0, nfc_dump_format(NULL, &cursor, s_text, 64));
    s_dump.n_pages = NFC_T2_MAX_PAGES + 1;
    TEST_ASSERT_EQUAL(0, nfc_dump_format(&s_dump, &cursor, s_text, 64));
}

static void test_dump_failures(void)
{
    dump_entry(0);
    nfc_tag_t tag = s_read.tag;
    fake_pn532_tag(&k_fake_catalog[3].tag); /* another tag */
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, nfc_reader_dump(&s_d, &tag, &s_dump));
    fake_pn532_tag(&k_fake_catalog[0].tag);
    fake_pn532_vanish_after(3); /* leaves mid-dump */
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, nfc_reader_dump(&s_d, &tag, &s_dump));
    TEST_ASSERT_EQUAL(12, s_dump.n_pages); /* three READs of four pages made it */
}

static const char *const k_bad[] = {
    "",
    "stark-nfc 2\n",
    "stark-nfc 1\nuid DEADBEE\n",
    "stark-nfc 1\nuid deadbeef\natqa 0004\nsak 08\ntech CLASSIC_1K\npages 0\n",
    "stark-nfc 1\nuid DEADBEEF\natqa 004\nsak 08\ntech CLASSIC_1K\npages 0\n",
    "stark-nfc 1\nuid DEADBEEF\natqa 0004\nsak 8\ntech CLASSIC_1K\npages 0\n",
    "stark-nfc 1\nuid DEADBEEF\natqa 0004\nsak 08\ntech CLASSIC_2K\npages 0\n",
    "stark-nfc 1\nuid DEADBEEF\natqa 0004\nsak 08\ntech CLASSIC_1K\npages 00\n",
    "stark-nfc 1\nuid DEADBEEF\natqa 0004\nsak 08\ntech CLASSIC_1K\npages 232\n",
    "stark-nfc 1\nuid DEADBEEF\natqa 0004\nsak 08\ntech CLASSIC_1K\npages 1\n",
    "stark-nfc 1\nuid DEADBEEF\natqa 0004\nsak 08\ntech NTAG213\npages 1\npage 1 00000000\n",
    "stark-nfc 1\nuid DEADBEEF\natqa 0004\nsak 08\ntech NTAG213\npages 1\npage 0 0000000\n",
    "stark-nfc 1\nuid DEADBEEF\natqa 0004\nsak 08\ntech NTAG213\npages 1\npage 0 0000000G\n",
    "stark-nfc 1\nuid DEADBEEF\natqa 0004\nsak 08\ntech NTAG213\npages 1\npage 00000000\n",
    "stark-nfc 1\nuid DEADBEEF\natqa 0004\nsak 08\ntech NTAG213\npages 1\npage x 00000000\n",
    "stark-nfc 1\nuid DEADBEEF\natqa 0004\nsak 08\ntech CLASSIC_1K\npages 0\n\n",
    "stark-nfc 1\nuid DEADBEEF\natqa 0004\nsak 08\ntech CLASSIC_1K\npages 0\nextra\n",
    "stark-nfc 1\natqa 0004\nuid DEADBEEF\nsak 08\ntech CLASSIC_1K\npages 0\n", /* order */
    "stark-nfc 1\nuid  DEADBEEF\natqa 0004\nsak 08\ntech CLASSIC_1K\npages 0\n",
    "stark-nfc 1\nuid\n",
};

static void test_malformed_rejected(void)
{
    for (size_t i = 0; i < sizeof k_bad / sizeof k_bad[0]; i++) {
        char label[16];
        snprintf(label, sizeof label, "bad %u", (unsigned)i);
        TEST_ASSERT_EQUAL_MESSAGE(STARK_ERR_INVALID_ARG,
                                  nfc_dump_parse(k_bad[i], strlen(k_bad[i]), &s_back), label);
    }
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, nfc_dump_parse(NULL, 0, &s_back));
    /* comments anywhere are skipped; a missing final newline is accepted */
    static const char ok[] = "# own tag\nstark-nfc 1\n# a note\nuid DEADBEEF\natqa 0004\n"
                             "sak 08\ntech CLASSIC_1K\npages 0";
    TEST_ASSERT_EQUAL(STARK_OK, nfc_dump_parse(ok, strlen(ok), &s_back));
}

/* Every cut of a valid dump fails cleanly, and seeded mutations never read
 * past the text (ASan/UBSan). */
static void test_truncation_and_fuzz(void)
{
    dump_entry(0);
    size_t n = format_all(&s_dump, 256);
    for (size_t cut = 0; cut + 1 < n; cut++) { /* the declared page count is never met */
        TEST_ASSERT_NOT_EQUAL(STARK_OK, nfc_dump_parse(s_text, cut, &s_back));
    }
    TEST_ASSERT_EQUAL(STARK_OK, nfc_dump_parse(s_text, n - 1, &s_back)); /* no final newline */
    static char buf[8192];
    uint32_t rng = 0x5EED0405u;
    unsigned parsed = 0;
    for (unsigned round = 0; round < 10000; round++) {
        memcpy(buf, s_text, n);
        unsigned flips = 1 + (rng >> 28);
        for (unsigned f = 0; f < flips; f++) {
            rng = rng * 1103515245u + 12345u;
            buf[(rng >> 8) % n] = (char)(rng >> 16);
        }
        rng = rng * 1103515245u + 12345u;
        size_t len = (rng >> 8) % 4 == 0 ? (rng >> 10) % (n + 1) : n;
        if (nfc_dump_parse(buf, len, &s_back) == STARK_OK) {
            parsed++;
        }
    }
    TEST_ASSERT_LESS_THAN(10000, parsed);
}

static void test_names(void)
{
    uint32_t highest = 0;
    TEST_ASSERT_TRUE(nfc_dump_name_scan(&highest, "nfc_0007.nfc"));
    TEST_ASSERT_TRUE(nfc_dump_name_scan(&highest, "nfc_0003.nfc"));
    TEST_ASSERT_EQUAL(7, highest);
    TEST_ASSERT_FALSE(nfc_dump_name_scan(&highest, "ir_0009.ir"));
    TEST_ASSERT_FALSE(nfc_dump_name_scan(&highest, "nfc_00a1.nfc"));
    TEST_ASSERT_FALSE(nfc_dump_name_scan(&highest, "nfc_0001.txt"));
    TEST_ASSERT_FALSE(nfc_dump_name_scan(&highest, NULL));
    char name[16];
    TEST_ASSERT_EQUAL(STARK_OK, nfc_dump_name_next(highest, name, sizeof name));
    TEST_ASSERT_EQUAL_STRING("nfc_0008.nfc", name);
    TEST_ASSERT_EQUAL(STARK_OK, nfc_dump_name_next(0, name, sizeof name));
    TEST_ASSERT_EQUAL_STRING("nfc_0001.nfc", name);
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, nfc_dump_name_next(9999, name, sizeof name));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, nfc_dump_name_next(1, name, 12));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_every_catalogue_tag);
    RUN_TEST(test_ndef_survives_the_file);
    RUN_TEST(test_format_text);
    RUN_TEST(test_dump_failures);
    RUN_TEST(test_malformed_rejected);
    RUN_TEST(test_truncation_and_fuzz);
    RUN_TEST(test_names);
    return UNITY_END();
}
