/*
 * test_nfc_ident.c — tag technology from ATQA/SAK/GET_VERSION (TASKS.md
 * STARK-0403). The catalogue's 25 tags are identified end to end in
 * test_nfc_reader.c; this covers the rules directly.
 */
#include "nfc_ident.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static const uint8_t k_ntag213[8] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x0F, 0x03};
static const uint8_t k_ntag215[8] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x11, 0x03};
static const uint8_t k_ntag216[8] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x13, 0x03};
static const uint8_t k_ntag_odd[8] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x15, 0x03};
static const uint8_t k_ul_ev1[8] = {0x00, 0x04, 0x03, 0x01, 0x01, 0x00, 0x0B, 0x03};
static const uint8_t k_nxp_other[8] = {0x00, 0x04, 0x07, 0x01, 0x01, 0x00, 0x0B, 0x03};
static const uint8_t k_vendor5[8] = {0x00, 0x05, 0x04, 0x02, 0x01, 0x00, 0x0F, 0x03};

static void test_type2(void)
{
    TEST_ASSERT_EQUAL(NFC_TECH_NTAG213, nfc_ident(0x0044, 0x00, k_ntag213));
    TEST_ASSERT_EQUAL(NFC_TECH_NTAG215, nfc_ident(0x0044, 0x00, k_ntag215));
    TEST_ASSERT_EQUAL(NFC_TECH_NTAG216, nfc_ident(0x0044, 0x00, k_ntag216));
    TEST_ASSERT_EQUAL(NFC_TECH_TYPE2, nfc_ident(0x0044, 0x00, k_ntag_odd));
    TEST_ASSERT_EQUAL(NFC_TECH_ULTRALIGHT, nfc_ident(0x0044, 0x00, k_ul_ev1));
    TEST_ASSERT_EQUAL(NFC_TECH_TYPE2, nfc_ident(0x0044, 0x00, k_nxp_other));
    TEST_ASSERT_EQUAL(NFC_TECH_TYPE2, nfc_ident(0x0044, 0x00, k_vendor5));
    TEST_ASSERT_EQUAL(NFC_TECH_ULTRALIGHT, nfc_ident(0x0044, 0x00, NULL));
    TEST_ASSERT_EQUAL(NFC_TECH_TYPE2, nfc_ident(0x0004, 0x00, NULL));
}

static void test_identification_only(void)
{
    TEST_ASSERT_EQUAL(NFC_TECH_CLASSIC_MINI, nfc_ident(0x0004, 0x09, NULL));
    TEST_ASSERT_EQUAL(NFC_TECH_CLASSIC_1K, nfc_ident(0x0004, 0x08, NULL));
    TEST_ASSERT_EQUAL(NFC_TECH_CLASSIC_1K, nfc_ident(0x0004, 0x88, NULL));
    TEST_ASSERT_EQUAL(NFC_TECH_CLASSIC_4K, nfc_ident(0x0002, 0x18, NULL));
    TEST_ASSERT_EQUAL(NFC_TECH_ISO_DEP, nfc_ident(0x0344, 0x20, NULL));
    TEST_ASSERT_EQUAL(NFC_TECH_ISO_DEP, nfc_ident(0x0344, 0x28, NULL)); /* Classic + ISO-DEP */
    TEST_ASSERT_EQUAL(NFC_TECH_UNKNOWN, nfc_ident(0x0004, 0x40, NULL));
    TEST_ASSERT_EQUAL(NFC_TECH_UNKNOWN, nfc_ident(0x0004, 0x01, k_ntag213)); /* version ignored */
}

static void test_names_and_pages(void)
{
    TEST_ASSERT_EQUAL_STRING("NTAG213", nfc_tech_name(NFC_TECH_NTAG213));
    TEST_ASSERT_EQUAL_STRING("ISO_DEP", nfc_tech_name(NFC_TECH_ISO_DEP));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", nfc_tech_name(NFC_TECH_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", nfc_tech_name((nfc_tech_t)99));
    TEST_ASSERT_EQUAL(45, nfc_tech_pages(NFC_TECH_NTAG213));
    TEST_ASSERT_EQUAL(135, nfc_tech_pages(NFC_TECH_NTAG215));
    TEST_ASSERT_EQUAL(231, nfc_tech_pages(NFC_TECH_NTAG216));
    TEST_ASSERT_EQUAL(0, nfc_tech_pages(NFC_TECH_ULTRALIGHT));
    TEST_ASSERT_TRUE(nfc_tech_is_type2(NFC_TECH_NTAG213));
    TEST_ASSERT_TRUE(nfc_tech_is_type2(NFC_TECH_TYPE2));
    TEST_ASSERT_FALSE(nfc_tech_is_type2(NFC_TECH_UNKNOWN));
    TEST_ASSERT_FALSE(nfc_tech_is_type2(NFC_TECH_CLASSIC_MINI));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_type2);
    RUN_TEST(test_identification_only);
    RUN_TEST(test_names_and_pages);
    return UNITY_END();
}
