/*
 * test_nfc_type2.c — the capability container and the TLV walk (TASKS.md
 * STARK-0403; NFC Forum Type 2 Tag §2-§4), on fixed data-area layouts.
 */
#include "nfc_type2.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static void test_cc(void)
{
    nfc_t2_cc_t cc = nfc_t2_cc((const uint8_t[]){0xE1, 0x10, 0x12, 0x00});
    TEST_ASSERT_TRUE(cc.valid);
    TEST_ASSERT_TRUE(cc.writable);
    TEST_ASSERT_EQUAL(144, cc.data_bytes);
    cc = nfc_t2_cc((const uint8_t[]){0xE1, 0x11, 0x6D, 0x0F}); /* version 1.1, read-only */
    TEST_ASSERT_TRUE(cc.valid);
    TEST_ASSERT_FALSE(cc.writable);
    TEST_ASSERT_EQUAL(872, cc.data_bytes);
    TEST_ASSERT_FALSE(nfc_t2_cc((const uint8_t[]){0xE2, 0x10, 0x12, 0x00}).valid); /* magic */
    TEST_ASSERT_FALSE(nfc_t2_cc((const uint8_t[]){0xE1, 0x20, 0x12, 0x00}).valid); /* 2.0 */
    TEST_ASSERT_FALSE(nfc_t2_cc((const uint8_t[]){0xE1, 0x10, 0x00, 0x00}).valid); /* size 0 */
    TEST_ASSERT_FALSE(nfc_t2_cc((const uint8_t[]){0xE1, 0x10, 0x12, 0x80}).valid); /* no read */
    TEST_ASSERT_EQUAL(0, nfc_t2_cc((const uint8_t[]){0xE2, 0x10, 0x12, 0x00}).data_bytes);
}

static void test_capacity(void)
{
    TEST_ASSERT_EQUAL(0, nfc_t2_ndef_capacity(0));
    TEST_ASSERT_EQUAL(0, nfc_t2_ndef_capacity(2));
    TEST_ASSERT_EQUAL(0, nfc_t2_ndef_capacity(3));
    TEST_ASSERT_EQUAL(141, nfc_t2_ndef_capacity(144)); /* NTAG213: 03 len msg FE */
    TEST_ASSERT_EQUAL(254, nfc_t2_ndef_capacity(257)); /* the largest short form */
    TEST_ASSERT_EQUAL(253, nfc_t2_ndef_capacity(258)); /* long form from 255 on */
    TEST_ASSERT_EQUAL(867, nfc_t2_ndef_capacity(872)); /* NTAG216 */
}

typedef struct {
    const char *label;
    uint8_t area[24];
    size_t avail, total;
    nfc_tlv_t expect;
    size_t off, len, need;
} tlv_case_t;

static const tlv_case_t k_cases[] = {
    {"ndef first", {0x03, 0x03, 0xD0, 0x00, 0x00, 0xFE}, 6, 6, NFC_TLV_FOUND, 2, 3, 0},
    {"after lock control",
     {0x01, 0x03, 0xA0, 0x0C, 0x34, 0x03, 0x01, 0xAA, 0xFE},
     9,
     9,
     NFC_TLV_FOUND,
     7,
     1,
     0},
    {"after memory control", {0x02, 0x03, 0, 0, 0, 0x03, 0x00, 0xFE}, 8, 8, NFC_TLV_FOUND, 7, 0, 0},
    {"after nulls", {0x00, 0x00, 0x03, 0x01, 0xAA}, 5, 5, NFC_TLV_FOUND, 4, 1, 0},
    {"long form", {0x03, 0xFF, 0x00, 0x05, 1, 2, 3, 4, 5}, 9, 20, NFC_TLV_FOUND, 4, 5, 0},
    {"value beyond what is read", {0x03, 0x10}, 2, 20, NFC_TLV_FOUND, 2, 16, 0},
    {"unknown tlv skipped", {0x10, 0x02, 0xAA, 0xBB, 0x03, 0x00}, 6, 6, NFC_TLV_FOUND, 6, 0, 0},
    {"terminator", {0xFE, 0x03, 0x01, 0xAA}, 4, 4, NFC_TLV_NONE, 0, 0, 0},
    {"end without ndef", {0x00, 0x00, 0x00}, 3, 3, NFC_TLV_NONE, 0, 0, 0},
    {"empty area", {0}, 0, 0, NFC_TLV_NONE, 0, 0, 0},
    {"ndef past the area", {0x03, 0x10, 0x00}, 3, 8, NFC_TLV_BAD, 0, 0, 0},
    {"long form past the area", {0x03, 0xFF, 0x0F, 0xFF}, 4, 12, NFC_TLV_BAD, 0, 0, 0},
    {"header cut by the area end", {0x00, 0x03}, 2, 2, NFC_TLV_BAD, 0, 0, 0},
    {"long header cut by the area end", {0x03, 0xFF, 0x00}, 3, 3, NFC_TLV_BAD, 0, 0, 0},
    {"more: type not read yet", {0x00, 0x00}, 2, 10, NFC_TLV_MORE, 0, 0, 3},
    {"more: length not read yet", {0x00, 0x03}, 2, 10, NFC_TLV_MORE, 0, 0, 3},
    {"more: long length not read yet", {0x03, 0xFF, 0x00}, 3, 10, NFC_TLV_MORE, 0, 0, 4},
    {"more: past a skipped tlv", {0x01, 0x03, 0xA0, 0x0C}, 4, 12, NFC_TLV_MORE, 0, 0, 6},
};

static void test_tlv_walk(void)
{
    for (size_t i = 0; i < sizeof k_cases / sizeof k_cases[0]; i++) {
        const tlv_case_t *c = &k_cases[i];
        size_t off = 0, len = 0, need = 0;
        TEST_ASSERT_EQUAL_MESSAGE(
            c->expect, nfc_t2_tlv(c->area, c->avail, c->total, &off, &len, &need), c->label);
        if (c->expect == NFC_TLV_FOUND) {
            TEST_ASSERT_EQUAL_MESSAGE(c->off, off, c->label);
            TEST_ASSERT_EQUAL_MESSAGE(c->len, len, c->label);
        }
        if (c->expect == NFC_TLV_MORE) {
            TEST_ASSERT_EQUAL_MESSAGE(c->need, need, c->label);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cc);
    RUN_TEST(test_capacity);
    RUN_TEST(test_tlv_walk);
    return UNITY_END();
}
