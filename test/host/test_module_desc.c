/*
 * test_module_desc.c — the ID EEPROM descriptor (TASKS.md STARK-0501): every
 * catalogue vector with its exact result, *out untouched on failure, a flip of
 * every bit, truncation at every length, and seeded mutations under sanitizers.
 */
#include <stdio.h>
#include <string.h>
#include "fake_module_catalog.h"
#include "module_desc.h"
#include "stark_caps.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static void test_catalogue(void)
{
    for (size_t i = 0; i < FAKE_MODULE_COUNT; i++) {
        const fake_module_entry_t *e = &k_fake_modules[i];
        module_desc_t d;
        memset(&d, 0xA5, sizeof d);
        module_desc_result_t r = module_desc_parse(e->image, MODULE_DESC_SIZE, &d);
        TEST_ASSERT_EQUAL_MESSAGE(e->result, r, e->label);
        if (r == MODULE_DESC_OK) {
            TEST_ASSERT_EQUAL_HEX16_MESSAGE(e->id, d.id, e->label);
            TEST_ASSERT_EQUAL_MESSAGE(e->rev, d.rev, e->label);
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(e->caps, d.caps, e->label);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(e->name, d.name, e->label);
            TEST_ASSERT_EQUAL_MESSAGE(e->supported, module_desc_caps_supported(d.caps), e->label);
        } else {
            for (size_t b = 0; b < sizeof d; b++) {
                TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xA5, ((uint8_t *)&d)[b], e->label);
            }
        }
    }
}

static void test_every_bit_flip_is_rejected(void)
{
    const fake_module_entry_t *e = &k_fake_modules[FAKE_MODULE_NFC_CARRIER - 1];
    for (size_t bit = 0; bit < MODULE_DESC_SIZE * 8; bit++) {
        uint8_t raw[MODULE_DESC_SIZE];
        memcpy(raw, e->image, sizeof raw);
        raw[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        module_desc_t d;
        TEST_ASSERT_NOT_EQUAL(MODULE_DESC_OK, module_desc_parse(raw, sizeof raw, &d));
    }
}

static void test_truncation_and_null(void)
{
    const uint8_t *img = k_fake_modules[0].image;
    module_desc_t d;
    for (size_t n = 0; n < MODULE_DESC_SIZE; n++) {
        TEST_ASSERT_EQUAL(MODULE_DESC_TRUNCATED, module_desc_parse(img, n, &d));
    }
    TEST_ASSERT_EQUAL(MODULE_DESC_TRUNCATED, module_desc_parse(NULL, MODULE_DESC_SIZE, &d));
    TEST_ASSERT_EQUAL(MODULE_DESC_TRUNCATED, module_desc_parse(img, MODULE_DESC_SIZE, NULL));
    TEST_ASSERT_EQUAL(MODULE_DESC_OK, module_desc_parse(img, 256, &d)); /* longer is fine */
}

static uint32_t s_seed = 0x5A17C0DEu;
static uint32_t rnd(void)
{
    s_seed ^= s_seed << 13;
    s_seed ^= s_seed >> 17;
    s_seed ^= s_seed << 5;
    return s_seed;
}

static void test_mutations(void)
{
    for (int i = 0; i < 10000; i++) {
        uint8_t raw[MODULE_DESC_SIZE];
        memcpy(raw, k_fake_modules[rnd() % FAKE_MODULE_COUNT].image, sizeof raw);
        for (uint32_t m = rnd() % 4; m > 0; m--) {
            raw[rnd() % sizeof raw] = (uint8_t)rnd();
        }
        module_desc_t d;
        if (module_desc_parse(raw, rnd() % (sizeof raw + 1), &d) == MODULE_DESC_OK) {
            TEST_ASSERT_TRUE(strlen(d.name) >= 1 && strlen(d.name) <= MODULE_NAME_MAX);
            for (const char *c = d.name; *c != '\0'; c++) {
                TEST_ASSERT_TRUE(*c >= 0x20 && *c <= 0x7E);
            }
            TEST_ASSERT_TRUE(d.id != 0 && d.id != 0xFFFFu);
        }
    }
}

static void test_caps_rules_and_strings(void)
{
    TEST_ASSERT_TRUE(module_desc_caps_supported(0));
    TEST_ASSERT_TRUE(module_desc_caps_supported(STARK_CAP_NFC));
    TEST_ASSERT_TRUE(module_desc_caps_supported(STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ));
    TEST_ASSERT_TRUE(module_desc_caps_supported(0xFFFFFFE0u)); /* unknown bits are not refused */
    TEST_ASSERT_FALSE(module_desc_caps_supported(STARK_CAP_IR_TX));
    TEST_ASSERT_FALSE(module_desc_caps_supported(STARK_CAP_IR_RX));
    TEST_ASSERT_FALSE(module_desc_caps_supported(STARK_CAP_NFC | STARK_CAP_MOD_SPI));
    TEST_ASSERT_EQUAL_STRING("ok", module_desc_result_str(MODULE_DESC_OK));
    TEST_ASSERT_EQUAL_STRING("blank", module_desc_result_str(MODULE_DESC_BLANK));
    TEST_ASSERT_EQUAL_STRING("bad magic", module_desc_result_str(MODULE_DESC_BAD_MAGIC));
    TEST_ASSERT_EQUAL_STRING("bad version", module_desc_result_str(MODULE_DESC_BAD_VERSION));
    TEST_ASSERT_EQUAL_STRING("bad crc", module_desc_result_str(MODULE_DESC_BAD_CRC));
    TEST_ASSERT_EQUAL_STRING("bad field", module_desc_result_str(MODULE_DESC_BAD_FIELD));
    TEST_ASSERT_EQUAL_STRING("truncated", module_desc_result_str(MODULE_DESC_TRUNCATED));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_catalogue);
    RUN_TEST(test_every_bit_flip_is_rejected);
    RUN_TEST(test_truncation_and_null);
    RUN_TEST(test_mutations);
    RUN_TEST(test_caps_rules_and_strings);
    return UNITY_END();
}
