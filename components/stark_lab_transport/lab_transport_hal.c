/*
 * lab_transport_hal.c — the lab transport on the hardware (stark_lab_transport.h): a thin
 * wrapper over stark_hal's pad calls. It holds no pin number: stark_board classifies, and
 * every call checks that class again (stark_lab_pad_ok()) before the HAL is touched.
 * Host-compilable (no ESP-IDF header): test_lab_service.c runs it over the fake HAL.
 */
#include "stark_hal.h"
#include "stark_lab_transport.h"

static bool s_open;

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
