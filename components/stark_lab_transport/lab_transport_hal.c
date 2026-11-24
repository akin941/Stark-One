/*
 * lab_transport_hal.c — the lab transport on the hardware (stark_lab_transport.h): a thin
 * wrapper over stark_hal's pad calls. It holds no pin number: stark_board classifies, and
 * every call checks that class again (stark_lab_pad_ok()) before the HAL is touched.
 * Host-compilable (no ESP-IDF header): test_lab_service.c runs it over the fake HAL.
 */
#include "stark_hal.h"
#include "stark_lab_transport.h"

#define PULSE_SLICE_MS 10u /* the longest wait between looks at the cancel flag */

static bool s_open;
static volatile bool s_cancel;

stark_err_t stark_lab_transport_open(void)
{
    s_open = true;
    return STARK_OK;
}

void stark_lab_transport_close(void)
{
    s_open = false;
}

stark_err_t stark_lab_transport_pad_in(int pin, uint8_t pull)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    switch (pull) {
        case STARK_LAB_PAD_NONE:
            return stark_hal_gpio_config_in(pin, STARK_HAL_PULL_NONE);
        case STARK_LAB_PAD_UP:
            return stark_hal_gpio_config_in(pin, STARK_HAL_PULL_UP);
        case STARK_LAB_PAD_DOWN:
            return stark_hal_gpio_config_in(pin, STARK_HAL_PULL_DOWN);
        default:
            return STARK_ERR_INVALID_ARG;
    }
}

stark_err_t stark_lab_transport_pad_out(int pin, bool level)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    return stark_hal_gpio_config_out_weak(pin, level);
}

stark_err_t stark_lab_transport_pad_read(int pin, bool *level)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (level == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    *level = stark_hal_gpio_read(pin);
    return STARK_OK;
}

stark_err_t stark_lab_transport_pad_hiz(int pin)
{
    if (!stark_lab_pad_hiz_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    return stark_hal_gpio_hiz(pin);
}

stark_err_t stark_lab_transport_pwm_start(int pin, uint32_t hz, uint8_t duty_pct)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (hz < STARK_LAB_PWM_HZ_MIN || hz > STARK_LAB_PWM_HZ_MAX || duty_pct > 100u) {
        return STARK_ERR_INVALID_ARG;
    }
    /* NOTE: the duty is set before the pad is driven weakly, and the channel is the lab's own
     * (STARK_LAB_PWM_CHANNEL): the buzzer's and the backlight's are never named here. */
    stark_err_t err = stark_hal_pwm_init(pin, hz, (uint8_t)STARK_LAB_PWM_CHANNEL);
    if (err == STARK_OK) {
        err = stark_hal_pwm_set_duty_pct((uint8_t)STARK_LAB_PWM_CHANNEL, duty_pct);
    }
    if (err == STARK_OK) {
        err = stark_hal_gpio_drive_weak(pin);
    }
    if (err != STARK_OK) {
        (void)stark_hal_pwm_stop((uint8_t)STARK_LAB_PWM_CHANNEL);
        (void)stark_hal_gpio_hiz(pin);
    }
    return err;
}

stark_err_t stark_lab_transport_pwm_stop(int pin)
{
    if (!stark_lab_pad_hiz_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    stark_err_t err = stark_hal_pwm_stop((uint8_t)STARK_LAB_PWM_CHANNEL);
    stark_err_t herr = stark_hal_gpio_hiz(pin);
    return err != STARK_OK ? err : herr;
}

stark_err_t stark_lab_transport_pulse(int pin, bool level, uint32_t us)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (us < STARK_LAB_PULSE_US_MIN || us > STARK_LAB_PULSE_US_MAX) {
        return STARK_ERR_INVALID_ARG;
    }
    s_cancel = false;
    stark_err_t err = stark_hal_gpio_config_out_weak(pin, level);
    if (err != STARK_OK) {
        return err;
    }
    if (us < 1000u) {
        stark_hal_delay_us(us);
    } else {
        for (uint32_t left = us / 1000u; left > 0u && !s_cancel;) {
            uint32_t slice = left < PULSE_SLICE_MS ? left : PULSE_SLICE_MS;
            stark_hal_delay_ms(slice);
            left -= slice;
        }
    }
    return stark_hal_gpio_config_out_weak(pin, !level);
}

void stark_lab_transport_pulse_cancel(void)
{
    s_cancel = true;
}

stark_err_t stark_lab_transport_pad_edges(int pin, uint32_t window_us, uint32_t *ts, size_t cap,
                                          size_t *n, uint32_t *seen, bool *first_level)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (window_us < STARK_LAB_EDGES_WINDOW_US_MIN || window_us > STARK_LAB_EDGES_WINDOW_US_MAX) {
        return STARK_ERR_INVALID_ARG;
    }
    return stark_hal_gpio_edges(pin, window_us, ts, cap, n, seen, first_level);
}

void stark_lab_transport_edges_cancel(void)
{
    stark_hal_gpio_edges_cancel();
}

uint32_t stark_lab_transport_tick_hz(void)
{
    return stark_hal_tick_hz();
}
