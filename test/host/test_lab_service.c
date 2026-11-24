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

/* ---- STARK-0702: the PWM and pulse calls ---------------------------------------------------- */

/* AC 1: every number from -1 to 64 against the PWM and pulse calls of the transport. */
static void pwm_pulse_sweep(bool slot_pins_offered)
{
    for (int pin = -1; pin <= 64; pin++) {
        bool offered = is_lab_pin(pin) || (slot_pins_offered && pin >= 40 && pin <= 42);
        bool classified = is_lab_pin(pin) || (pin >= 40 && pin <= 42);
        unsigned before = hal_host_pad_calls();
        hal_host_pwm_reset();
        stark_err_t e_pwm = stark_lab_transport_pwm_start(pin, 1000, 50);
        stark_err_t e_pulse = stark_lab_transport_pulse(pin, true, 100);
        if (!offered) {
            TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, e_pwm);
            TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, e_pulse);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(before, hal_host_pad_calls(),
                                           "an unoffered pin reached the HAL");
            TEST_ASSERT_FALSE_MESSAGE(hal_host_pwm_last_init()->called,
                                      "an unoffered pin reached LEDC");
        } else {
            TEST_ASSERT_EQUAL(STARK_OK, e_pwm);
            TEST_ASSERT_EQUAL(STARK_OK, e_pulse);
        }
        /* stopping only releases: any classified pin, never a reserved one */
        hal_host_pwm_reset();
        stark_err_t e_stop = stark_lab_transport_pwm_stop(pin);
        TEST_ASSERT_EQUAL(classified ? STARK_OK : STARK_ERR_NOT_SUPPORTED, e_stop);
        TEST_ASSERT_EQUAL_UINT(classified ? 1u : 0u, hal_host_pwm_stops());
    }
}

static void test_the_pwm_and_pulse_sweep(void)
{
    pwm_pulse_sweep(true);
    set_slot0(STARK_MODULE_GENERIC);
    pwm_pulse_sweep(false);
    set_slot0(STARK_MODULE_KNOWN);
    pwm_pulse_sweep(false);
}

/* AC 4: LEDC channel 2 only, the weakest drive, the duty set, the pad released after. */
static void test_pwm_uses_the_lab_channel_only_and_stop_releases_the_pad(void)
{
    hal_host_pwm_reset();
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pwm_start(38, 2500, 40));
    const hal_host_pwm_init_call_t *init = hal_host_pwm_last_init();
    TEST_ASSERT_TRUE(init->called);
    TEST_ASSERT_EQUAL(38, init->pin);
    TEST_ASSERT_EQUAL_UINT32(2500, init->hz);
    TEST_ASSERT_EQUAL_UINT8(STARK_LAB_PWM_CHANNEL, init->channel);
    TEST_ASSERT_EQUAL_UINT8(2, init->channel); /* neither 0, the buzzer, nor 1, the backlight */
    TEST_ASSERT_EQUAL_UINT8(2, hal_host_pwm_last_set_duty()->channel);
    TEST_ASSERT_EQUAL_UINT8(40, hal_host_pwm_last_set_duty()->pct);
    TEST_ASSERT_FALSE(hal_host_pwm_last_set_freq()->called);
    TEST_ASSERT_EQUAL(HAL_HOST_PAD_PWM, hal_host_pad(38)->mode);
    TEST_ASSERT_TRUE(hal_host_pad(38)->weak);
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pwm_stop(38));
    TEST_ASSERT_TRUE(hal_host_pwm_last_stop()->called);
    TEST_ASSERT_EQUAL_UINT8(2, hal_host_pwm_last_stop()->channel);
    TEST_ASSERT_EQUAL_UINT(1, hal_host_pwm_stops());
    TEST_ASSERT_EQUAL(HAL_HOST_PAD_IN, hal_host_pad(38)->mode); /* the pad is hi-Z again */
    TEST_ASSERT_EQUAL(STARK_HAL_PULL_NONE, hal_host_pad(38)->pull);
}

static void test_pwm_arguments_at_every_edge(void)
{
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_pwm_start(38, 99, 50));
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pwm_start(38, 100, 0));
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pwm_start(38, 40000, 100));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_pwm_start(38, 40001, 50));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_pwm_start(38, 1000, 101));
    TEST_ASSERT_EQUAL_UINT32(40000, hal_host_pwm_last_init()->hz);
}

static void test_a_failing_pwm_start_stops_the_channel_and_releases_the_pad(void)
{
    const unsigned stages[] = {HAL_HOST_FAIL_PWM_INIT, HAL_HOST_FAIL_PWM_DUTY,
                               HAL_HOST_FAIL_DRIVE_WEAK};
    for (size_t i = 0; i < 3; i++) {
        hal_host_gpio_reset();
        hal_host_pwm_reset();
        hal_host_fail(stages[i]);
        TEST_ASSERT_EQUAL(STARK_ERR_IO, stark_lab_transport_pwm_start(39, 1000, 50));
        TEST_ASSERT_EQUAL_UINT(1, hal_host_pwm_stops());
        TEST_ASSERT_EQUAL_UINT8(2, hal_host_pwm_last_stop()->channel);
        TEST_ASSERT_EQUAL(HAL_HOST_PAD_IN, hal_host_pad(39)->mode); /* released */
    }
    hal_host_gpio_reset();
}

static void test_pulse_widths_waits_and_the_pad_after(void)
{
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_pulse(38, true, 9));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_pulse(38, true, 1000001));
    TEST_ASSERT_EQUAL_UINT(0, hal_host_pad_calls());
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pulse(38, true, 10)); /* a busy wait */
    TEST_ASSERT_EQUAL_UINT64(10u, hal_host_delayed_us());
    TEST_ASSERT_EQUAL(HAL_HOST_PAD_OUT, hal_host_pad(38)->mode);
    TEST_ASSERT_TRUE(hal_host_pad(38)->weak);
    TEST_ASSERT_FALSE(hal_host_pad(38)->level); /* it rests at the opposite level */
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pulse(38, false, 100000));
    TEST_ASSERT_TRUE(hal_host_pad(38)->level);
    hal_host_gpio_reset();
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pulse(39, true, 1000)); /* the task delay */
    TEST_ASSERT_EQUAL_UINT64(1000u, hal_host_delayed_us());
    hal_host_gpio_reset();
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pulse(39, true, 1000000));
    TEST_ASSERT_EQUAL_UINT64(1000000u, hal_host_delayed_us()); /* the width, in slices of 10 ms */
    hal_host_gpio_reset();
}

static void cancel_at_the_third_slice(unsigned call)
{
    if (call == 3) {
        stark_lab_transport_pulse_cancel();
    }
}

/* A cancelled pulse ends within a slice and still rests at the opposite level. */
static void test_a_pulse_can_be_cut_short(void)
{
    hal_host_set_delay_hook(cancel_at_the_third_slice);
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pulse(38, true, 1000000));
    TEST_ASSERT_EQUAL_UINT64(30000u, hal_host_delayed_us()); /* three 10 ms slices, not 1 s */
    TEST_ASSERT_FALSE(hal_host_pad(38)->level);
    hal_host_set_delay_hook(NULL);
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pulse(38, true, 100000)); /* not sticky */
    TEST_ASSERT_EQUAL_UINT64(130000u, hal_host_delayed_us());
}

static void test_a_failing_pulse_drive_is_reported(void)
{
    hal_host_fail(HAL_HOST_FAIL_OUT_WEAK);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, stark_lab_transport_pulse(38, true, 100));
    TEST_ASSERT_EQUAL_UINT64(0u, hal_host_delayed_us()); /* nothing was waited */
    hal_host_fail(0);
}

static void test_a_closed_transport_refuses_pwm_and_pulse(void)
{
    hal_host_pwm_reset();
    stark_lab_transport_close();
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, stark_lab_transport_pwm_start(38, 1000, 50));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, stark_lab_transport_pulse(38, true, 100));
    TEST_ASSERT_FALSE(hal_host_pwm_last_init()->called);
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_pwm_stop(38)); /* releasing is always allowed */
}

/* ---- STARK-0703: the edge capture ----------------------------------------------------------- */

/* AC 3 (the pin half): every number from -1 to 64 against the capture; an unoffered pin never
 * reaches the HAL, so no interrupt is ever installed on a pad another owner has. */
static void edges_sweep(bool slot_pins_offered)
{
    for (int pin = -1; pin <= 64; pin++) {
        bool offered = is_lab_pin(pin) || (slot_pins_offered && pin >= 40 && pin <= 42);
        uint32_t ring[8];
        size_t n = 0;
        uint32_t seen = 0;
        bool first = false;
        unsigned before = hal_host_edges_calls();
        stark_err_t err = stark_lab_transport_pad_edges(pin, 20000, ring, 8, &n, &seen, &first);
        if (offered) {
            TEST_ASSERT_EQUAL(STARK_OK, err);
            TEST_ASSERT_EQUAL_UINT(before + 1u, hal_host_edges_calls());
        } else {
            TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, err);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(before, hal_host_edges_calls(),
                                           "an unoffered pin reached the interrupt");
        }
    }
}

static void test_the_edge_capture_sweep(void)
{
    edges_sweep(true);
    set_slot0(STARK_MODULE_GENERIC);
    edges_sweep(false);
    set_slot0(STARK_MODULE_KNOWN);
    edges_sweep(false);
    set_slot0(STARK_MODULE_CONFLICT);
    edges_sweep(false);
}

static void test_the_capture_window_is_bounded_and_the_arguments_are_checked(void)
{
    TEST_ASSERT_EQUAL_UINT32(STARK_LAB_EDGES_GATE_MAX, STARK_HAL_EDGES_GATE_MAX);
    uint32_t ring[8];
    size_t n;
    uint32_t seen;
    bool first;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_pad_edges(38, 9999, ring, 8, &n, &seen, &first));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_pad_edges(38, 4000001u, ring, 8, &n, &seen, &first));
    TEST_ASSERT_EQUAL_UINT(0, hal_host_edges_calls());
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_lab_transport_pad_edges(38, 10000, ring, 8, &n, &seen, &first));
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_lab_transport_pad_edges(38, 4000000u, ring, 8, &n, &seen, &first));
    TEST_ASSERT_EQUAL_UINT32(4000000u, hal_host_edges_last()->window_us);
    /* the HAL checks the rest */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_pad_edges(38, 20000, NULL, 8, &n, &seen, &first));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_pad_edges(38, 20000, ring, 8, NULL, &seen, &first));
}

static int s_irq_in_window = -2;

static void note_irq(int pin)
{
    (void)pin;
    s_irq_in_window = hal_host_edge_irq_pin();
}

static void cancel_in_window(int pin)
{
    (void)pin;
    stark_lab_transport_edges_cancel();
}

static void test_the_capture_reaches_the_hal_and_the_interrupt_is_gone_afterwards(void)
{
    static const uint32_t stamps[3] = {5, 6, 7};
    uint32_t ring[8];
    size_t n = 0;
    uint32_t seen = 0;
    bool first = false;
    hal_host_edges_set(stamps, 3, 3, true);
    hal_host_set_edges_hook(note_irq);
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_lab_transport_pad_edges(39, 20000, ring, 8, &n, &seen, &first));
    TEST_ASSERT_EQUAL_INT(39, s_irq_in_window);
    TEST_ASSERT_EQUAL_INT(-1, hal_host_edge_irq_pin());
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_UINT32(3, seen);
    TEST_ASSERT_TRUE(first);
    TEST_ASSERT_EQUAL_UINT32(7, ring[2]);
    TEST_ASSERT_EQUAL_INT(39, hal_host_edges_last()->pin);
    TEST_ASSERT_EQUAL_UINT32(20000, hal_host_edges_last()->window_us);
    TEST_ASSERT_EQUAL_UINT32(240000000u, stark_lab_transport_tick_hz());
    /* a pad is not touched by a capture */
    TEST_ASSERT_EQUAL_UINT(0, hal_host_pad_calls());
}

static void test_a_capture_can_be_cut_short_and_a_fault_is_reported(void)
{
    uint32_t ring[8];
    size_t n = 0;
    uint32_t seen = 0;
    bool first = false;
    hal_host_set_edges_hook(cancel_in_window);
    TEST_ASSERT_EQUAL(STARK_ERR_STATE,
                      stark_lab_transport_pad_edges(38, 4000000u, ring, 8, &n, &seen, &first));
    TEST_ASSERT_EQUAL_INT(-1, hal_host_edge_irq_pin());
    hal_host_set_edges_hook(NULL);
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_lab_transport_pad_edges(38, 20000, ring, 8, &n, &seen, &first));
    hal_host_fail(HAL_HOST_FAIL_EDGES);
    TEST_ASSERT_EQUAL(STARK_ERR_IO,
                      stark_lab_transport_pad_edges(38, 20000, ring, 8, &n, &seen, &first));
    hal_host_fail(0);
}

static void test_a_closed_transport_refuses_the_capture(void)
{
    uint32_t ring[8];
    size_t n;
    uint32_t seen;
    bool first;
    stark_lab_transport_close();
    TEST_ASSERT_EQUAL(STARK_ERR_STATE,
                      stark_lab_transport_pad_edges(38, 20000, ring, 8, &n, &seen, &first));
    TEST_ASSERT_EQUAL_UINT(0, hal_host_edges_calls());
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
    RUN_TEST(test_the_pwm_and_pulse_sweep);
    RUN_TEST(test_pwm_uses_the_lab_channel_only_and_stop_releases_the_pad);
    RUN_TEST(test_pwm_arguments_at_every_edge);
    RUN_TEST(test_a_failing_pwm_start_stops_the_channel_and_releases_the_pad);
    RUN_TEST(test_pulse_widths_waits_and_the_pad_after);
    RUN_TEST(test_a_pulse_can_be_cut_short);
    RUN_TEST(test_a_failing_pulse_drive_is_reported);
    RUN_TEST(test_a_closed_transport_refuses_pwm_and_pulse);
    RUN_TEST(test_the_edge_capture_sweep);
    RUN_TEST(test_the_capture_window_is_bounded_and_the_arguments_are_checked);
    RUN_TEST(test_the_capture_reaches_the_hal_and_the_interrupt_is_gone_afterwards);
    RUN_TEST(test_a_capture_can_be_cut_short_and_a_fault_is_reported);
    RUN_TEST(test_a_closed_transport_refuses_the_capture);
    return UNITY_END();
}
