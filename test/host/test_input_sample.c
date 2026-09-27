/*
 * test_input_sample.c — key GPIO levels -> raw bitmap (input_sample.c)
 * against the fake HAL. The target emulator cannot drive external GPIO
 * inputs, so pin order and active-low polarity are proven here; electrical
 * wiring is a hardware (HIL) check (docs/VALIDATION.md).
 */
#include <string.h>
#include "input_sample.h"
#include "stark_hal.h"
#include "stark_hal_host_control.h"
#include "stark_input.h"
#include "unity.h"

/* The DevKitC-1 key pins (board_devkitc1.c; diagram.json is checked
 * against them by scripts/check_pins.py): UP DOWN LEFT RIGHT OK BACK. */
static const int k_pins[STARK_KEY_COUNT] = {4, 5, 6, 7, 15, 16};
static stark_board_pins_t s_pins;

void setUp(void)
{
    hal_host_gpio_reset();
    memset(&s_pins, 0, sizeof s_pins);
    for (int k = 0; k < STARK_KEY_COUNT; k++) {
        s_pins.key[k] = k_pins[k];
        /* as stark_board_init() configures them: inputs with pull-ups */
        TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_config_input(k_pins[k], true));
    }
}

void tearDown(void)
{}

static void press(stark_key_t k)
{
    stark_hal_gpio_write(k_pins[k], false); /* shorted to ground */
}

static void test_idle_pulled_up_reads_no_key(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, input_sample_raw(&s_pins));
}

static void test_each_key_maps_to_its_bit(void)
{
    for (int k = 0; k < STARK_KEY_COUNT; k++) {
        setUp();
        press((stark_key_t)k);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(1u << k), input_sample_raw(&s_pins));
    }
}

static void test_ok_is_gpio15_back_is_gpio16(void)
{
    stark_hal_gpio_write(15, false);
    TEST_ASSERT_EQUAL_HEX8(1u << STARK_KEY_OK, input_sample_raw(&s_pins));
    stark_hal_gpio_write(15, true);
    stark_hal_gpio_write(16, false);
    TEST_ASSERT_EQUAL_HEX8(1u << STARK_KEY_BACK, input_sample_raw(&s_pins));
}

static void test_simultaneous_keys_combine(void)
{
    press(STARK_KEY_UP);
    press(STARK_KEY_OK);
    TEST_ASSERT_EQUAL_HEX8((1u << STARK_KEY_UP) | (1u << STARK_KEY_OK), input_sample_raw(&s_pins));
    for (int k = 0; k < STARK_KEY_COUNT; k++) {
        press((stark_key_t)k);
    }
    TEST_ASSERT_EQUAL_HEX8(0x3F, input_sample_raw(&s_pins));
}

static void test_high_level_is_released(void)
{
    press(STARK_KEY_LEFT);
    stark_hal_gpio_write(k_pins[STARK_KEY_LEFT], true);
    TEST_ASSERT_EQUAL_HEX8(0x00, input_sample_raw(&s_pins));
}

static void test_absent_key_reads_released(void)
{
    s_pins.key[STARK_KEY_RIGHT] = -1; /* the board lacks it */
    TEST_ASSERT_EQUAL_HEX8(0x00, input_sample_raw(&s_pins));
    press(STARK_KEY_DOWN);
    TEST_ASSERT_EQUAL_HEX8(1u << STARK_KEY_DOWN, input_sample_raw(&s_pins));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_idle_pulled_up_reads_no_key);
    RUN_TEST(test_each_key_maps_to_its_bit);
    RUN_TEST(test_ok_is_gpio15_back_is_gpio16);
    RUN_TEST(test_simultaneous_keys_combine);
    RUN_TEST(test_high_level_is_released);
    RUN_TEST(test_absent_key_reads_released);
    return UNITY_END();
}
