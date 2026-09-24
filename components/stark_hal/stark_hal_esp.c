/*
 * stark_hal_esp.c — ESP-IDF implementation of stark_hal.h + error
 * translation (stark_hal_esp.h). Target-only.
 */
#include "stark_hal.h"
#include "stark_hal_esp.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 10-bit duty resolution (0-1023): enough range for both a buzzer's
 * square-wave-ish tone and backlight brightness, one setting for both
 * of V0's two PWM consumers (see stark_hal.h channel allocation note). */
#define STARK_HAL_PWM_DUTY_RES LEDC_TIMER_10_BIT
#define STARK_HAL_PWM_MAX_DUTY ((1u << STARK_HAL_PWM_DUTY_RES) - 1)

/* ---- Error translation --------------------------------------------- */

stark_err_t stark_err_from_esp(esp_err_t err)
{
    switch (err) {
        case ESP_OK:
            return STARK_OK;
        case ESP_ERR_NO_MEM:
            return STARK_ERR_NO_MEM;
        case ESP_ERR_INVALID_ARG:
            return STARK_ERR_INVALID_ARG;
        case ESP_ERR_INVALID_SIZE:
            return STARK_ERR_INVALID_ARG;
        case ESP_ERR_INVALID_MAC:
            return STARK_ERR_INVALID_ARG;
        case ESP_ERR_INVALID_STATE:
            return STARK_ERR_STATE;
        case ESP_ERR_NOT_FOUND:
            return STARK_ERR_NOT_FOUND;
        case ESP_ERR_NOT_SUPPORTED:
            return STARK_ERR_NOT_SUPPORTED;
        case ESP_ERR_INVALID_VERSION:
            return STARK_ERR_NOT_SUPPORTED;
        case ESP_ERR_TIMEOUT:
            return STARK_ERR_TIMEOUT;
        case ESP_ERR_NOT_FINISHED:
            return STARK_ERR_BUSY;
        case ESP_ERR_NOT_ALLOWED:
            return STARK_ERR_NOT_SUPPORTED;
        case ESP_ERR_INVALID_RESPONSE:
            return STARK_ERR_IO;
        case ESP_ERR_INVALID_CRC:
            return STARK_ERR_IO;
        case ESP_FAIL:
            return STARK_ERR_IO;
        default:
            return STARK_ERR_IO;
    }
}

/* ---- Time ------------------------------------------------------------- */

uint64_t stark_hal_now_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

void stark_hal_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* ---- GPIO --------------------------------------------------------------
 * Failures return stark_err_from_esp(err); config validity beyond plain
 * range (e.g. a pin this specific board doesn't wire up) is
 * stark_board_validate()'s job, not this layer's. Range itself must still
 * be checked here, before it's used to build a shift amount: `1ULL <<
 * pin` for a negative pin is undefined behaviour in C, not just an
 * ESP-IDF error — gpio_config()'s own internal validation runs too late
 * to prevent that. */

#define STARK_HAL_GPIO_IN_RANGE(pin) ((pin) >= 0 && (pin) < SOC_GPIO_PIN_COUNT)

stark_err_t stark_hal_gpio_config_input(int pin, bool pullup)
{
    if (!STARK_HAL_GPIO_IN_RANGE(pin)) {
        return STARK_ERR_INVALID_ARG;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return stark_err_from_esp(gpio_config(&cfg));
}

stark_err_t stark_hal_gpio_config_output(int pin, bool initial)
{
    if (!STARK_HAL_GPIO_IN_RANGE(pin)) {
        return STARK_ERR_INVALID_ARG;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return stark_err_from_esp(err);
    }
    return stark_err_from_esp(gpio_set_level(pin, initial ? 1 : 0));
}

bool stark_hal_gpio_read(int pin)
{
    /* gpio_get_level() does no validation of its own and takes gpio_num
     * as uint32_t internally: a negative pin silently becomes a huge
     * unsigned value, which the HAL's bit-shift on it then reads with
     * undefined behaviour (a shift-by-more-than-width). Guard here. */
    if (!STARK_HAL_GPIO_IN_RANGE(pin)) {
        return false;
    }
    return gpio_get_level(pin) != 0;
}

void stark_hal_gpio_write(int pin, bool level)
{
    if (!STARK_HAL_GPIO_IN_RANGE(pin)) {
        return;
    }
    (void)gpio_set_level(pin, level ? 1 : 0);
}

/* ---- PWM (LEDC) --------------------------------------------------------
 * One low-speed timer per channel — simplest allocation for V0's two
 * fixed-assignment channels (stark_hal.h). */

stark_err_t stark_hal_pwm_init(int pin, uint32_t hz, uint8_t channel)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = STARK_HAL_PWM_DUTY_RES,
        .timer_num = (ledc_timer_t)channel,
        .freq_hz = hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        return stark_err_from_esp(err);
    }

    ledc_channel_config_t ch_cfg = {
        .gpio_num = pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = (ledc_channel_t)channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = (ledc_timer_t)channel,
        .duty = 0,
        .hpoint = 0,
    };
    return stark_err_from_esp(ledc_channel_config(&ch_cfg));
}

stark_err_t stark_hal_pwm_set_freq(uint8_t channel, uint32_t hz)
{
    return stark_err_from_esp(ledc_set_freq(LEDC_LOW_SPEED_MODE, (ledc_timer_t)channel, hz));
}

stark_err_t stark_hal_pwm_set_duty_pct(uint8_t channel, uint8_t pct)
{
    if (pct > 100) {
        pct = 100;
    }
    uint32_t duty = (STARK_HAL_PWM_MAX_DUTY * pct) / 100;

    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel, duty);
    if (err != ESP_OK) {
        return stark_err_from_esp(err);
    }
    return stark_err_from_esp(ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel));
}