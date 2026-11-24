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

/* ---- Lab pads (STARK-0701) ---------------------------------------------- */

void test_stark_hal_pad_calls_record_mode_pull_and_drive(void)
{
    TEST_ASSERT_EQUAL(HAL_HOST_PAD_NONE, hal_host_pad(38)->mode);
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_config_in(38, STARK_HAL_PULL_UP));
    TEST_ASSERT_EQUAL(HAL_HOST_PAD_IN, hal_host_pad(38)->mode);
    TEST_ASSERT_EQUAL(STARK_HAL_PULL_UP, hal_host_pad(38)->pull);
    TEST_ASSERT_TRUE(stark_hal_gpio_read(38)); /* nothing external: the pull-up */
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_config_in(38, STARK_HAL_PULL_DOWN));
    TEST_ASSERT_FALSE(stark_hal_gpio_read(38));
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_config_out_weak(38, true));
    TEST_ASSERT_EQUAL(HAL_HOST_PAD_OUT, hal_host_pad(38)->mode);
    TEST_ASSERT_TRUE(hal_host_pad(38)->weak);
    TEST_ASSERT_TRUE(hal_host_pad(38)->level);
    TEST_ASSERT_TRUE(stark_hal_gpio_read(38)); /* read back */
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_hiz(38));
    TEST_ASSERT_EQUAL(HAL_HOST_PAD_IN, hal_host_pad(38)->mode);
    TEST_ASSERT_EQUAL(STARK_HAL_PULL_NONE, hal_host_pad(38)->pull);
    TEST_ASSERT_FALSE(stark_hal_gpio_read(38)); /* floating reads low */
    TEST_ASSERT_EQUAL_UINT(4, hal_host_pad_calls());
    hal_host_gpio_reset();
    TEST_ASSERT_EQUAL_UINT(0, hal_host_pad_calls());
    TEST_ASSERT_EQUAL(HAL_HOST_PAD_NONE, hal_host_pad(38)->mode);
}

void test_stark_hal_pad_calls_reject_out_of_range_pins(void)
{
    const int bad[] = {-1, 49, 64};
    for (size_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                          stark_hal_gpio_config_in(bad[i], STARK_HAL_PULL_UP));
        TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_hal_gpio_config_out_weak(bad[i], true));
        TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_hal_gpio_hiz(bad[i]));
        TEST_ASSERT_NULL(hal_host_pad(bad[i]));
    }
    TEST_ASSERT_EQUAL_UINT(0, hal_host_pad_calls());
}

/* ---- STARK-0702: delay, weak drive, PWM stop, injected failures --------------------------- */

void test_stark_hal_delays_are_counted_and_never_wait(void)
{
    stark_hal_delay_us(250);
    stark_hal_delay_ms(3);
    TEST_ASSERT_EQUAL_UINT64(3250u, hal_host_delayed_us());
    hal_host_gpio_reset();
    TEST_ASSERT_EQUAL_UINT64(0u, hal_host_delayed_us());
}

static unsigned s_hook_calls;
static void hook(unsigned call)
{
    s_hook_calls = call;
}

void test_stark_hal_delay_hook_runs_after_each_ms_delay(void)
{
    s_hook_calls = 0;
    hal_host_set_delay_hook(hook);
    stark_hal_delay_ms(1);
    stark_hal_delay_ms(1);
    TEST_ASSERT_EQUAL_UINT(2, s_hook_calls);
    hal_host_set_delay_hook(NULL);
    stark_hal_delay_ms(1);
    TEST_ASSERT_EQUAL_UINT(2, s_hook_calls);
}

void test_stark_hal_drive_weak_marks_the_pad_and_rejects_bad_pins(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_drive_weak(38));
    TEST_ASSERT_TRUE(hal_host_pad(38)->weak);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_hal_gpio_drive_weak(-1));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_hal_gpio_drive_weak(49));
}

void test_stark_hal_pwm_stop_records_only_its_own_channel(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_pwm_init(38, 1000, 2));
    TEST_ASSERT_EQUAL(HAL_HOST_PAD_PWM, hal_host_pad(38)->mode);
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_pwm_set_duty_pct(0, 50)); /* the buzzer's channel */
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_pwm_set_duty_pct(1, 80)); /* the backlight's */
    TEST_ASSERT_FALSE(hal_host_pwm_last_stop()->called);
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_pwm_stop(2));
    TEST_ASSERT_TRUE(hal_host_pwm_last_stop()->called);
    TEST_ASSERT_EQUAL_UINT8(2, hal_host_pwm_last_stop()->channel);
    TEST_ASSERT_EQUAL_UINT(1, hal_host_pwm_stops());
    TEST_ASSERT_EQUAL_UINT8(1, hal_host_pwm_last_set_duty()->channel); /* untouched by the stop */
    hal_host_pwm_reset();
    TEST_ASSERT_EQUAL_UINT(0, hal_host_pwm_stops());
}

void test_stark_hal_failures_can_be_injected_per_call(void)
{
    hal_host_fail(HAL_HOST_FAIL_PWM_INIT);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, stark_hal_pwm_init(38, 1000, 2));
    hal_host_fail(HAL_HOST_FAIL_PWM_DUTY);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, stark_hal_pwm_set_duty_pct(2, 50));
    hal_host_fail(HAL_HOST_FAIL_DRIVE_WEAK);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, stark_hal_gpio_drive_weak(38));
    hal_host_fail(HAL_HOST_FAIL_OUT_WEAK);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, stark_hal_gpio_config_out_weak(38, true));
    hal_host_fail(0);
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_config_out_weak(38, true));
    hal_host_fail(HAL_HOST_FAIL_PWM_INIT);
    hal_host_gpio_reset(); /* the reset clears the failures too */
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_pwm_init(38, 1000, 2));
}

/* ---- STARK-0703: edge capture --------------------------------------------------------------- */

static int s_hook_pin = -2;
static int s_irq_seen_in_hook = -2;

static void edges_hook(int pin)
{
    s_hook_pin = pin;
    s_irq_seen_in_hook = hal_host_edge_irq_pin();
}

static void edges_cancel_hook(int pin)
{
    (void)pin;
    stark_hal_gpio_edges_cancel();
}

void test_stark_hal_edges_returns_what_it_was_given_within_the_callers_ring(void)
{
    static const uint32_t stamps[6] = {10, 20, 30, 40, 50, 60};
    uint32_t ring[4];
    size_t n = 99;
    uint32_t seen = 99;
    bool first = false;
    hal_host_edges_set(stamps, 6, 6, true);
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_edges(38, 20000, ring, 4, &n, &seen, &first));
    TEST_ASSERT_EQUAL_UINT(4, n);      /* the ring holds four */
    TEST_ASSERT_EQUAL_UINT32(5, seen); /* the count saturates at cap + 1, as on the target */
    TEST_ASSERT_TRUE(first);
    TEST_ASSERT_EQUAL_UINT32(40, ring[3]);
    TEST_ASSERT_EQUAL_UINT(1, hal_host_edges_calls());
    TEST_ASSERT_EQUAL_INT(38, hal_host_edges_last()->pin);
    TEST_ASSERT_EQUAL_UINT32(20000, hal_host_edges_last()->window_us);
    TEST_ASSERT_EQUAL_UINT(4, hal_host_edges_last()->cap);
    TEST_ASSERT_EQUAL_UINT64(20000, hal_host_delayed_us()); /* the window is counted, not waited */
    /* a gate: cap 0, no ring, counts up to its own limit */
    hal_host_edges_set(stamps, 6, 9000, false);
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_edges(39, 20000, NULL, 0, &n, &seen, &first));
    TEST_ASSERT_EQUAL_UINT(0, n);
    TEST_ASSERT_EQUAL_UINT32(STARK_HAL_EDGES_GATE_MAX, seen);
    TEST_ASSERT_FALSE(first);
    /* a ring bigger than what the fake holds stores what there is */
    uint32_t big[16];
    hal_host_edges_set(stamps, 6, 6, false);
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_edges(38, 20000, big, 16, &n, &seen, &first));
    TEST_ASSERT_EQUAL_UINT(6, n);
    TEST_ASSERT_EQUAL_UINT32(6, seen);
    TEST_ASSERT_EQUAL_UINT32(240000000u, stark_hal_tick_hz());
}

void test_stark_hal_edges_interrupt_is_on_for_the_window_only(void)
{
    uint32_t ring[4];
    size_t n;
    uint32_t seen;
    bool first;
    hal_host_edges_set(NULL, 0, 0, false);
    hal_host_set_edges_hook(edges_hook);
    TEST_ASSERT_EQUAL_INT(-1, hal_host_edge_irq_pin());
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_edges(39, 10000, ring, 4, &n, &seen, &first));
    TEST_ASSERT_EQUAL_INT(39, s_hook_pin);
    TEST_ASSERT_EQUAL_INT(39, s_irq_seen_in_hook);      /* on during the window */
    TEST_ASSERT_EQUAL_INT(-1, hal_host_edge_irq_pin()); /* and removed after it */
    /* removed on a cancel and on a failure too */
    hal_host_set_edges_hook(edges_cancel_hook);
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, stark_hal_gpio_edges(39, 10000, ring, 4, &n, &seen, &first));
    TEST_ASSERT_EQUAL_INT(-1, hal_host_edge_irq_pin());
    hal_host_set_edges_hook(NULL);
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_hal_gpio_edges(39, 10000, ring, 4, &n, &seen, &first)); /* not sticky */
    hal_host_fail(HAL_HOST_FAIL_EDGES);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, stark_hal_gpio_edges(39, 10000, ring, 4, &n, &seen, &first));
    TEST_ASSERT_EQUAL_INT(-1, hal_host_edge_irq_pin());
    hal_host_fail(0);
}

void test_stark_hal_edges_reports_the_level_the_pad_had(void)
{
    uint32_t ring[4];
    size_t n;
    uint32_t seen;
    bool first;
    hal_host_edges_set(NULL, 0, 0, false);
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_config_in(38, STARK_HAL_PULL_UP));
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_edges(38, 10000, ring, 4, &n, &seen, &first));
    TEST_ASSERT_TRUE(hal_host_edges_last()->level_before);
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_hiz(38));
    TEST_ASSERT_EQUAL(STARK_OK, stark_hal_gpio_edges(38, 10000, ring, 4, &n, &seen, &first));
    TEST_ASSERT_FALSE(hal_host_edges_last()->level_before);
    TEST_ASSERT_EQUAL(HAL_HOST_PAD_IN, hal_host_pad(38)->mode); /* the pad is left as it was */
}

void test_stark_hal_edges_rejects_bad_arguments_before_anything_happens(void)
{
    uint32_t ring[4];
    size_t n;
    uint32_t seen;
    bool first;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_hal_gpio_edges(-1, 10000, ring, 4, &n, &seen, &first));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_hal_gpio_edges(49, 10000, ring, 4, &n, &seen, &first));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_hal_gpio_edges(38, 0, ring, 4, &n, &seen, &first));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_hal_gpio_edges(38, 4100001u, ring, 4, &n, &seen, &first));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_hal_gpio_edges(38, 10000, NULL, 4, &n, &seen, &first));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_hal_gpio_edges(38, 10000, ring, 4, NULL, &seen, &first));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_hal_gpio_edges(38, 10000, ring, 4, &n, NULL, &first));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_hal_gpio_edges(38, 10000, ring, 4, &n, &seen, NULL));
    TEST_ASSERT_EQUAL_UINT(0, hal_host_edges_calls());
    TEST_ASSERT_EQUAL_UINT64(0, hal_host_delayed_us());
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
    RUN_TEST(test_stark_hal_pad_calls_record_mode_pull_and_drive);
    RUN_TEST(test_stark_hal_pad_calls_reject_out_of_range_pins);
    RUN_TEST(test_stark_hal_delays_are_counted_and_never_wait);
    RUN_TEST(test_stark_hal_delay_hook_runs_after_each_ms_delay);
    RUN_TEST(test_stark_hal_drive_weak_marks_the_pad_and_rejects_bad_pins);
    RUN_TEST(test_stark_hal_pwm_stop_records_only_its_own_channel);
    RUN_TEST(test_stark_hal_failures_can_be_injected_per_call);
    RUN_TEST(test_stark_hal_edges_returns_what_it_was_given_within_the_callers_ring);
    RUN_TEST(test_stark_hal_edges_interrupt_is_on_for_the_window_only);
    RUN_TEST(test_stark_hal_edges_reports_the_level_the_pad_had);
    RUN_TEST(test_stark_hal_edges_rejects_bad_arguments_before_anything_happens);
    return UNITY_END();
}
