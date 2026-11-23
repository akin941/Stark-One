/*
 * test_lab_service.c — the production lab transport (components/stark_lab_transport/
 * lab_transport_hal.c) over the fake HAL (TASKS.md STARK-0701, ADR-0023 §2–3): the second half
 * of "from the service and again from the transport". A pin that is not offered is refused
 * before the HAL is touched, for every call and every number from -1 to 64, and every offered
 * call reaches the HAL with the weakest drive and no simulator conditional anywhere.
 * stark_board's class table comes from the real stark_board.c; the module table is injected.
 */
#include <string.h>
#include "stark_board.h"
#include "stark_hal_host_control.h"
#include "stark_lab_transport.h"
#include "unity.h"

static stark_board_pins_t s_map;
static stark_module_entry_t s_table[STARK_MODULE_ENTRIES];
static size_t s_table_n;
static size_t s_list_report; /* what stark_module_list() returns; 0: s_table_n */

const stark_board_pins_t *stark_board_pins(void)
{
    return &s_map;
}

size_t stark_module_list(stark_module_entry_t *out, size_t max)
{
    size_t n = s_table_n < max ? s_table_n : max;
    memcpy(out, s_table, n * sizeof(*out));
    return s_list_report != 0 ? s_list_report : n;
}

static void set_slot0(stark_module_state_t state)
{
    memset(s_table, 0, sizeof(s_table));
    s_table[0] = (stark_module_entry_t){.kind = STARK_MODULE_SLOT, .state = state, .slot = 0};
    s_table[1] =
        (stark_module_entry_t){.kind = STARK_MODULE_SLOT, .state = STARK_MODULE_ABSENT, .slot = 1};
    s_table_n = STARK_MODULE_ENTRIES;
    s_list_report = 0;
}

void setUp(void)
{
    s_map = (stark_board_pins_t){
        .lab_uart_tx = 38,
        .lab_uart_rx = 39,
        .module = {{.cs = 40, .irq0 = 41, .irq1 = 42, .en = -1, .eeprom_addr = 0x50},
                   {.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1, .eeprom_addr = 0x51}},
    };
    hal_host_gpio_reset();
    set_slot0(STARK_MODULE_ABSENT);
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_open());
}

void tearDown(void)
{
    stark_lab_transport_close();
}

static bool is_lab_pin(int pin)
{
    return pin == 38 || pin == 39;
}

/* AC 1: every number from -1 to 64 against every transport call. */
static void sweep(bool slot_pins_offered)
{
    for (int pin = -1; pin <= 64; pin++) {
        bool offered = is_lab_pin(pin) || (slot_pins_offered && pin >= 40 && pin <= 42);
        bool classified = is_lab_pin(pin) || (pin >= 40 && pin <= 42);
        bool level = false;
        unsigned before = hal_host_pad_calls();
        stark_err_t e_in = stark_lab_transport_pad_in(pin, STARK_LAB_PAD_UP);
        stark_err_t e_out = stark_lab_transport_pad_out(pin, true);
        stark_err_t e_rd = stark_lab_transport_pad_read(pin, &level);
        if (!offered) {
            TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, e_in);
            TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, e_out);
            TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, e_rd);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(before, hal_host_pad_calls(),
                                           "an unoffered pin reached the HAL");
        } else {
            TEST_ASSERT_EQUAL(STARK_OK, e_in);
            TEST_ASSERT_EQUAL(STARK_OK, e_out);
            TEST_ASSERT_EQUAL(STARK_OK, e_rd);
        }
        /* hi-Z only releases: allowed for any classified pin, never for a reserved one */
        unsigned mid = hal_host_pad_calls();
        stark_err_t e_hz = stark_lab_transport_pad_hiz(pin);
        if (classified) {
            TEST_ASSERT_EQUAL(STARK_OK, e_hz);
            TEST_ASSERT_EQUAL_UINT(mid + 1, hal_host_pad_calls());
        } else {
            TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, e_hz);
            TEST_ASSERT_EQUAL_UINT(mid, hal_host_pad_calls());
        }
    }
}

static void test_sweep_with_slot_0_empty(void)
{
    sweep(true);
}

static void test_sweep_with_slot_0_generic_known_conflict(void)
{
    set_slot0(STARK_MODULE_GENERIC);
    sweep(false);
    set_slot0(STARK_MODULE_KNOWN);
    sweep(false);
    set_slot0(STARK_MODULE_CONFLICT);
    sweep(false);
}

static void test_an_empty_or_unstarted_module_table_offers_no_slot_pin(void)
{
    s_table_n = 0;
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, stark_lab_transport_pad_in(40, STARK_LAB_PAD_NONE));
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pad_in(38, STARK_LAB_PAD_NONE));
    TEST_ASSERT_EQUAL_UINT(1, hal_host_pad_calls());
}

static void test_a_table_that_reports_too_many_entries_is_clamped(void)
{
    set_slot0(STARK_MODULE_ABSENT);
    s_list_report = STARK_MODULE_ENTRIES + 9;
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pad_in(40, STARK_LAB_PAD_NONE));
}

static void test_slot_1_has_no_pin_and_an_unlisted_slot_is_not_absent(void)
{
    s_table_n = 0;
    TEST_ASSERT_FALSE(stark_lab_slot_absent(s_table, 0, 0));
    set_slot0(STARK_MODULE_ABSENT);
    TEST_ASSERT_TRUE(stark_lab_slot_absent(s_table, STARK_MODULE_ENTRIES, 0));
    TEST_ASSERT_TRUE(stark_lab_slot_absent(s_table, STARK_MODULE_ENTRIES, 1));
    TEST_ASSERT_FALSE(stark_lab_slot_absent(s_table, STARK_MODULE_ENTRIES, 2));
    s_table[2].kind = STARK_MODULE_ONBOARD; /* a provider is not a slot */
    s_table[2].slot = 2;
    TEST_ASSERT_FALSE(stark_lab_slot_absent(s_table, STARK_MODULE_ENTRIES, 2));
}

static void test_the_offer_rule(void)
{
    TEST_ASSERT_TRUE(stark_lab_pin_offered(STARK_PIN_LAB, false));
    TEST_ASSERT_TRUE(stark_lab_pin_offered(STARK_PIN_LAB, true));
    TEST_ASSERT_TRUE(stark_lab_pin_offered(STARK_PIN_SLOT, true));
    TEST_ASSERT_FALSE(stark_lab_pin_offered(STARK_PIN_SLOT, false));
    TEST_ASSERT_FALSE(stark_lab_pin_offered(STARK_PIN_RESERVED, true));
    TEST_ASSERT_FALSE(stark_lab_pin_offered(STARK_PIN_RESERVED, false));
}

static void test_calls_reach_the_hal_with_the_weakest_drive(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pad_in(38, STARK_LAB_PAD_UP));
    TEST_ASSERT_EQUAL(HAL_HOST_PAD_IN, hal_host_pad(38)->mode);
    TEST_ASSERT_EQUAL(STARK_HAL_PULL_UP, hal_host_pad(38)->pull);
    bool level = false;
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pad_read(38, &level));
    TEST_ASSERT_TRUE(level);
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pad_in(38, STARK_LAB_PAD_DOWN));
    TEST_ASSERT_EQUAL(STARK_HAL_PULL_DOWN, hal_host_pad(38)->pull);
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pad_in(38, STARK_LAB_PAD_NONE));
    TEST_ASSERT_EQUAL(STARK_HAL_PULL_NONE, hal_host_pad(38)->pull);
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pad_out(39, true));
    TEST_ASSERT_EQUAL(HAL_HOST_PAD_OUT, hal_host_pad(39)->mode);
    TEST_ASSERT_TRUE(hal_host_pad(39)->weak);
    TEST_ASSERT_TRUE(hal_host_pad(39)->level);
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pad_read(39, &level));
    TEST_ASSERT_TRUE(level); /* read back */
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pad_hiz(39));
    TEST_ASSERT_EQUAL(HAL_HOST_PAD_IN, hal_host_pad(39)->mode);
    TEST_ASSERT_EQUAL(STARK_HAL_PULL_NONE, hal_host_pad(39)->pull);
}

static void test_bad_arguments_never_reach_the_hal(void)
{
    unsigned before = hal_host_pad_calls();
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_pad_in(38, 3));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_pad_read(38, NULL));
    TEST_ASSERT_EQUAL_UINT(before, hal_host_pad_calls());
}

static void test_a_closed_transport_refuses_everything_but_release(void)
{
    stark_lab_transport_close();
    bool level;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, stark_lab_transport_pad_in(38, STARK_LAB_PAD_NONE));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, stark_lab_transport_pad_out(38, true));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, stark_lab_transport_pad_read(38, &level));
    TEST_ASSERT_EQUAL_UINT(0, hal_host_pad_calls());
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pad_hiz(38)); /* releasing is always allowed */
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_open());
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pad_in(38, STARK_LAB_PAD_NONE));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sweep_with_slot_0_empty);
    RUN_TEST(test_sweep_with_slot_0_generic_known_conflict);
    RUN_TEST(test_an_empty_or_unstarted_module_table_offers_no_slot_pin);
    RUN_TEST(test_a_table_that_reports_too_many_entries_is_clamped);
    RUN_TEST(test_slot_1_has_no_pin_and_an_unlisted_slot_is_not_absent);
    RUN_TEST(test_the_offer_rule);
    RUN_TEST(test_calls_reach_the_hal_with_the_weakest_drive);
    RUN_TEST(test_bad_arguments_never_reach_the_hal);
    RUN_TEST(test_a_closed_transport_refuses_everything_but_release);
    return UNITY_END();
}
