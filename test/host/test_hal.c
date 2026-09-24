/*
 * test_hal.c — host unit tests for the fake stark_hal (STARK-0007).
 * Exercises test/host/support/stark_hal_host.c through stark_hal.h (the
 * same header stark_hal_esp.c implements) plus the test-only control
 * surface in stark_hal_host_control.h.
 */
#include "stark_hal.h"
#include "stark_hal_host_control.h"
#include "unity.h"

void setUp(void)
{
    hal_host_set_now_us(0);
    hal_host_gpio_reset();
    hal_host_pwm_reset();
}

void tearDown(void)
{}

/* ---- Time: advances only when told ------------------------------------ */

void test_stark_hal_now_us_does_not_advance_on_its_own(void)
{
    TEST_ASSERT_EQUAL_UINT64(0, stark_hal_now_us());
    TEST_ASSERT_EQUAL_UINT64(0, stark_hal_now_us());
    TEST_ASSERT_EQUAL_UINT64(0, stark_hal_now_us());
}

void test_stark_hal_now_us_reflects_last_set_value(void)
{
    hal_host_set_now_us(1000);
    TEST_ASSERT_EQUAL_UINT64(1000, stark_hal_now_us());

    hal_host_set_now_us(2500);
    TEST_ASSERT_EQUAL_UINT64(2500, stark_hal_now_us());
}

/* ---- GPIO: read returns the last written value ------------------------- */

void test_stark_hal_gpio_read_returns_last_written_value(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_config_output(18, false));
    TEST_ASSERT_FALSE(stark_hal_gpio_read(18));

    stark_hal_gpio_write(18, true);
    TEST_ASSERT_TRUE(stark_hal_gpio_read(18));

    stark_hal_gpio_write(18, false);
    TEST_ASSERT_FALSE(stark_hal_gpio_read(18));
}

void test_stark_hal_gpio_config_output_sets_initial_level(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_config_output(17, true));
    TEST_ASSERT_TRUE(stark_hal_gpio_read(17));
}

void test_stark_hal_gpio_config_input_pullup_reads_high_when_floating(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_config_input(4, true));
    TEST_ASSERT_TRUE(stark_hal_gpio_read(4));
}

void test_stark_hal_gpio_config_input_no_pullup_reads_low_when_floating(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_config_input(4, false));
    TEST_ASSERT_FALSE(stark_hal_gpio_read(4));
}

void test_stark_hal_gpio_rejects_invalid_pin(void)
{
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_hal_gpio_config_output(-1, false));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_hal_gpio_config_input(99, false));
}

/* ---- PWM: calls record their arguments for inspection ------------------ */

void test_stark_hal_pwm_init_records_arguments(void)
{
    TEST_ASSERT_FALSE(hal_host_pwm_last_init()->called);

    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_pwm_init(17, 3000, 0));

    const hal_host_pwm_init_call_t *call = hal_host_pwm_last_init();
    TEST_ASSERT_TRUE(call->called);
    TEST_ASSERT_EQUAL(17, call->pin);
    TEST_ASSERT_EQUAL_UINT32(3000, call->hz);
    TEST_ASSERT_EQUAL_UINT8(0, call->channel);
}

void test_stark_hal_pwm_set_freq_records_arguments(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_pwm_set_freq(1, 5000));

    const hal_host_pwm_set_freq_call_t *call = hal_host_pwm_last_set_freq();
    TEST_ASSERT_TRUE(call->called);
    TEST_ASSERT_EQUAL_UINT8(1, call->channel);
    TEST_ASSERT_EQUAL_UINT32(5000, call->hz);
}

void test_stark_hal_pwm_set_duty_pct_records_arguments(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_pwm_set_duty_pct(0, 75));

    const hal_host_pwm_set_duty_call_t *call = hal_host_pwm_last_set_duty();
    TEST_ASSERT_TRUE(call->called);
    TEST_ASSERT_EQUAL_UINT8(0, call->channel);
    TEST_ASSERT_EQUAL_UINT8(75, call->pct);
}

void test_stark_hal_pwm_set_duty_pct_clamps_above_100(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_pwm_set_duty_pct(0, 250));
    TEST_ASSERT_EQUAL_UINT8(100, hal_host_pwm_last_set_duty()->pct);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stark_hal_now_us_does_not_advance_on_its_own);
    RUN_TEST(test_stark_hal_now_us_reflects_last_set_value);
    RUN_TEST(test_stark_hal_gpio_read_returns_last_written_value);
    RUN_TEST(test_stark_hal_gpio_config_output_sets_initial_level);
    RUN_TEST(test_stark_hal_gpio_config_input_pullup_reads_high_when_floating);
    RUN_TEST(test_stark_hal_gpio_config_input_no_pullup_reads_low_when_floating);
    RUN_TEST(test_stark_hal_gpio_rejects_invalid_pin);
    RUN_TEST(test_stark_hal_pwm_init_records_arguments);
    RUN_TEST(test_stark_hal_pwm_set_freq_records_arguments);
    RUN_TEST(test_stark_hal_pwm_set_duty_pct_records_arguments);
    RUN_TEST(test_stark_hal_pwm_set_duty_pct_clamps_above_100);
    return UNITY_END();
}
