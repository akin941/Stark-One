/*
 * test_fake_24c02.c — the test-only EEPROM itself (TASKS.md STARK-0501): the
 * word-address pointer, wrap at 256, presence per address, and every fault.
 */
#include <string.h>
#include "fake_24c02.h"
#include "unity.h"

#define ADDR 0x50

void setUp(void)
{
    fake_24c02_reset();
}
void tearDown(void)
{}

static void test_absent_nacks(void)
{
    uint8_t b[4];
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_24c02_probe(ADDR));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_24c02_probe(0x10));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_24c02_read(ADDR, b, 4));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_24c02_write(ADDR, b, 1));
    fake_24c02_put(ADDR, NULL, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_24c02_probe(ADDR));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_24c02_probe(ADDR + 1));
    fake_24c02_remove(ADDR);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_24c02_probe(ADDR));
}

static void test_pointer_and_wrap(void)
{
    uint8_t image[FAKE_24C02_SIZE];
    for (size_t i = 0; i < sizeof image; i++) {
        image[i] = (uint8_t)i;
    }
    fake_24c02_put(ADDR, image, sizeof image);
    uint8_t b[4];
    TEST_ASSERT_EQUAL(STARK_OK, fake_24c02_read(ADDR, b, 2));
    TEST_ASSERT_EQUAL_HEX8(1, b[1]);
    TEST_ASSERT_EQUAL(STARK_OK, fake_24c02_read(ADDR, b, 1)); /* current-address read */
    TEST_ASSERT_EQUAL_HEX8(2, b[0]);
    TEST_ASSERT_EQUAL(STARK_OK, fake_24c02_read_at(ADDR, 254, b, 4)); /* wraps */
    static const uint8_t want[] = {254, 255, 0, 1};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, b, 4);
    const uint8_t wr[] = {10, 0xAB};
    TEST_ASSERT_EQUAL(STARK_OK, fake_24c02_write(ADDR, wr, sizeof wr));
    TEST_ASSERT_EQUAL(STARK_OK, fake_24c02_read_at(ADDR, 10, b, 1));
    TEST_ASSERT_EQUAL_HEX8(0xAB, b[0]);
}

static void test_erased_beyond_image(void)
{
    const uint8_t img[] = {1, 2};
    fake_24c02_put(ADDR, img, sizeof img);
    uint8_t b[3];
    TEST_ASSERT_EQUAL(STARK_OK, fake_24c02_read_at(ADDR, 0, b, 3));
    TEST_ASSERT_EQUAL_HEX8(0xFF, b[2]);
}

static void test_faults(void)
{
    uint8_t b[8];
    fake_24c02_put(ADDR, NULL, 0);
    fake_24c02_fault(FAKE_24C02_NACK_ADDR, 1);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, fake_24c02_probe(ADDR));
    TEST_ASSERT_EQUAL(STARK_OK, fake_24c02_probe(ADDR)); /* count ran out */
    fake_24c02_fault(FAKE_24C02_TIMEOUT, 2);
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, fake_24c02_probe(ADDR));
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, fake_24c02_read(ADDR, b, 8));
    fake_24c02_fault(FAKE_24C02_BUS_ERROR, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_24c02_probe(ADDR));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_24c02_write(ADDR, b, 1));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_24c02_read(ADDR, b, 8));
    fake_24c02_fault(FAKE_24C02_NONE, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_24c02_probe(ADDR));
    fake_24c02_fault(FAKE_24C02_NACK_DATA, -1);
    TEST_ASSERT_EQUAL(STARK_OK, fake_24c02_probe(ADDR)); /* the address still answers */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_24c02_read(ADDR, b, 8));
    fake_24c02_fault(FAKE_24C02_TRUNCATE, 1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_24c02_read(ADDR, b, 8));
    fake_24c02_fault(FAKE_24C02_NONE, 0);
    uint8_t clean[8];
    TEST_ASSERT_EQUAL(STARK_OK, fake_24c02_read_at(ADDR, 0, clean, 8));
    fake_24c02_fault(FAKE_24C02_BIT_FLIP, 1);
    TEST_ASSERT_EQUAL(STARK_OK, fake_24c02_read(ADDR, b, 8));
    TEST_ASSERT_EQUAL_HEX8(clean[4] ^ 0x10u, b[4]);
    TEST_ASSERT_TRUE(fake_24c02_transactions() > 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_absent_nacks);
    RUN_TEST(test_pointer_and_wrap);
    RUN_TEST(test_erased_beyond_image);
    RUN_TEST(test_faults);
    return UNITY_END();
}
