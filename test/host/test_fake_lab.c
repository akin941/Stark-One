/*
 * test_fake_lab.c — the test-only pad model (TASKS.md STARK-0701, ADR-0023 §2): hi-Z, input
 * with a pull, output with a level, external sources, wires and faults. The model carries the
 * evidence of every host and emulator test that uses it, so it is tested on its own first.
 */
#include "fake_lab.h"
#include "unity.h"

#define PIN_A 38
#define PIN_B 39

void setUp(void)
{
    fake_lab_reset();
}
void tearDown(void)
{}

static bool level(int pin)
{
    bool v = false;
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_read(pin, &v));
    return v;
}

static void test_power_on_pads_are_hiz_and_read_low(void)
{
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(PIN_A));
    TEST_ASSERT_FALSE(level(PIN_A)); /* floating reads low */
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_calls());
}

static void test_input_follows_its_pull(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 1));
    TEST_ASSERT_TRUE(level(PIN_A));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 2));
    TEST_ASSERT_FALSE(level(PIN_A));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 0));
    TEST_ASSERT_FALSE(level(PIN_A));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_IN, fake_lab_mode(PIN_A));
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pull(PIN_A));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 2));
    TEST_ASSERT_EQUAL_UINT8(2, fake_lab_pull(PIN_A));
}

static void test_output_reads_back_its_level_and_hiz_releases_it(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, true));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_OUT, fake_lab_mode(PIN_A));
    TEST_ASSERT_TRUE(fake_lab_out_level(PIN_A));
    TEST_ASSERT_TRUE(level(PIN_A));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, false));
    TEST_ASSERT_FALSE(level(PIN_A));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, true));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_hiz(PIN_A));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(PIN_A));
    TEST_ASSERT_FALSE(fake_lab_out_level(PIN_A));
    TEST_ASSERT_FALSE(level(PIN_A));
}

static void test_an_external_source_wins_on_an_input_and_fights_an_output(void)
{
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_HIGH, 0, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 2));
    TEST_ASSERT_TRUE(level(PIN_A)); /* against the pull-down */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, false));
    TEST_ASSERT_TRUE(level(PIN_A)); /* contention: the pad does not follow the output */
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_LOW, 0, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 1));
    TEST_ASSERT_FALSE(level(PIN_A)); /* against the pull-up */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, false));
    TEST_ASSERT_FALSE(level(PIN_A)); /* the same level: no contention */
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_NONE, 0, 0);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, true));
    TEST_ASSERT_TRUE(level(PIN_A));
}

static void test_square_wave_follows_the_injected_clock(void)
{
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 1000, 25); /* 1 ms period, 250 us high */
    fake_lab_time_us(0);
    TEST_ASSERT_TRUE(level(PIN_A));
    fake_lab_time_us(249);
    TEST_ASSERT_TRUE(level(PIN_A));
    fake_lab_time_us(250);
    TEST_ASSERT_FALSE(level(PIN_A));
    fake_lab_time_us(999);
    TEST_ASSERT_FALSE(level(PIN_A));
    fake_lab_time_us(1000 * 7 + 100);
    TEST_ASSERT_TRUE(level(PIN_A));
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 0, 50); /* no frequency: a 1 µs period, always high */
    TEST_ASSERT_TRUE(level(PIN_A));
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_SQUARE, 4000000, 50); /* faster than 1 MHz: the same floor */
    TEST_ASSERT_TRUE(level(PIN_A));
}

static void test_a_wire_carries_an_output_to_another_pad(void)
{
    fake_lab_wire(PIN_A, PIN_B);
    TEST_ASSERT_FALSE(level(PIN_B)); /* PIN_A is not an output yet */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, true));
    TEST_ASSERT_TRUE(level(PIN_B));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_B, 1));
    TEST_ASSERT_TRUE(level(PIN_B));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, false));
    TEST_ASSERT_FALSE(level(PIN_B)); /* it drives low against the pull-up */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_hiz(PIN_A));
    TEST_ASSERT_TRUE(level(PIN_B)); /* released: the pull again */
    fake_lab_wire(-1, -1);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, false));
    TEST_ASSERT_TRUE(level(PIN_B)); /* no wire any more */
}

static void test_wire_limits_and_precedence(void)
{
    fake_lab_wire(100, 1); /* ignored: outside the chip */
    fake_lab_wire(1, 100);
    fake_lab_wire(1, 2);
    fake_lab_wire(3, 4);
    fake_lab_wire(5, 6);
    fake_lab_wire(7, 8);
    fake_lab_wire(9, 10); /* a fifth: ignored */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(9, true));
    TEST_ASSERT_FALSE(level(10));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(7, true));
    TEST_ASSERT_TRUE(level(8));
    fake_lab_ext(8, FAKE_LAB_EXT_LOW, 0, 0); /* an explicit source beats a wire */
    TEST_ASSERT_FALSE(level(8));
}

static void test_stuck_faults_count_down_and_filter_by_pin(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, true));
    fake_lab_fault(FAKE_LAB_FAULT_PAD_STUCK_LOW, PIN_A, 2);
    TEST_ASSERT_FALSE(level(PIN_A));
    TEST_ASSERT_FALSE(level(PIN_A));
    TEST_ASSERT_TRUE(level(PIN_A)); /* the fault is spent */
    fake_lab_fault(FAKE_LAB_FAULT_PAD_STUCK_HIGH, PIN_B, -1);
    TEST_ASSERT_TRUE(level(PIN_B));
    TEST_ASSERT_TRUE(level(PIN_B));
    TEST_ASSERT_TRUE(level(PIN_A)); /* another pad: unaffected (it is high anyway) */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, false));
    TEST_ASSERT_FALSE(level(PIN_A));
    fake_lab_fault(FAKE_LAB_FAULT_PAD_STUCK_HIGH, -1, 1); /* any pad */
    TEST_ASSERT_TRUE(level(PIN_A));
    TEST_ASSERT_FALSE(level(PIN_A));
    fake_lab_fault(FAKE_LAB_FAULT_PAD_STUCK_HIGH, -1, -1);
    fake_lab_fault(FAKE_LAB_FAULT_NONE, -1, 0); /* cleared */
    TEST_ASSERT_FALSE(level(PIN_A));
}

static void test_config_error_fails_pad_in_and_out_but_never_hiz(void)
{
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, PIN_A, 2);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_lab_pad_in(PIN_A, 1));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_lab_pad_out(PIN_A, true));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(PIN_A)); /* nothing changed */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, true));
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, -1, -1);
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_hiz(PIN_A));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, fake_lab_pad_in(PIN_B, 0));
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, PIN_B, 1); /* a fault for another pad */
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 0));
}

static void test_bad_arguments_touch_nothing(void)
{
    bool v;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_in(-1, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_in(49, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_in(PIN_A, 3));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_out(-1, true));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_out(49, true));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_hiz(-1));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_hiz(49));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_read(-1, &v));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_read(49, &v));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, fake_lab_pad_read(PIN_A, NULL));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_calls());
    fake_lab_ext(-1, FAKE_LAB_EXT_HIGH, 0, 0); /* ignored */
    fake_lab_ext(49, FAKE_LAB_EXT_HIGH, 0, 0);
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(-1));
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pull(49));
    TEST_ASSERT_FALSE(fake_lab_out_level(-1));
}

static void test_reset_returns_to_power_on(void)
{
    fake_lab_ext(PIN_A, FAKE_LAB_EXT_HIGH, 0, 0);
    fake_lab_wire(PIN_A, PIN_B);
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, -1, -1);
    fake_lab_time_us(55);
    fake_lab_reset();
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_calls());
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_in(PIN_A, 0));
    TEST_ASSERT_FALSE(level(PIN_A));
    TEST_ASSERT_EQUAL(STARK_OK, fake_lab_pad_out(PIN_A, true));
    TEST_ASSERT_FALSE(level(PIN_B));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_power_on_pads_are_hiz_and_read_low);
    RUN_TEST(test_input_follows_its_pull);
    RUN_TEST(test_output_reads_back_its_level_and_hiz_releases_it);
    RUN_TEST(test_an_external_source_wins_on_an_input_and_fights_an_output);
    RUN_TEST(test_square_wave_follows_the_injected_clock);
    RUN_TEST(test_a_wire_carries_an_output_to_another_pad);
    RUN_TEST(test_wire_limits_and_precedence);
    RUN_TEST(test_stuck_faults_count_down_and_filter_by_pin);
    RUN_TEST(test_config_error_fails_pad_in_and_out_but_never_hiz);
    RUN_TEST(test_bad_arguments_touch_nothing);
    RUN_TEST(test_reset_returns_to_power_on);
    return UNITY_END();
}
