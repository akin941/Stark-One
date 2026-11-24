/*
 * stark_hal_esp.c — ESP-IDF implementation of stark_hal.h + error
 * translation (stark_hal_esp.h). Target-only.
 */
#include "stark_hal.h"
#include "stark_hal_esp.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_rom_sys.h"
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

void stark_hal_delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
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

/*
 * NOTE: gpio_config() reserves a pad it makes an output (esp_gpio_reserve) and warns `gpio:
 * conflict found for GPIO[n]` on every later output configuration of the same pad, a reservation
 * that stays until gpio_reset_pin() revokes it (esp-emu 0.44.0 run, docs/VALIDATION.md §2). The
 * lab pads therefore track the pads this HAL made outputs (and the pads it routed to LEDC, which
 * reserves them the same way): a drive of a pad that is already an
 * output only sets the level, and any other configuration of it resets the pin first, which also
 * detaches whatever the pad was routed to. Callers are serialised (the lab service's mutex).
 */
static uint64_t s_lab_out_mask;

static void lab_release_output(int pin)
{
    if (((s_lab_out_mask >> pin) & 1u) != 0u) {
        (void)gpio_reset_pin((gpio_num_t)pin);
        s_lab_out_mask &= ~(1ULL << pin);
    }
}

stark_err_t stark_hal_gpio_config_in(int pin, stark_hal_pull_t pull)
{
    if (!STARK_HAL_GPIO_IN_RANGE(pin)) {
        return STARK_ERR_INVALID_ARG;
    }
    lab_release_output(pin);
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pull == STARK_HAL_PULL_UP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pull == STARK_HAL_PULL_DOWN ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return stark_err_from_esp(gpio_config(&cfg));
}

stark_err_t stark_hal_gpio_hiz(int pin)
{
    return stark_hal_gpio_config_in(pin, STARK_HAL_PULL_NONE);
}

stark_err_t stark_hal_gpio_drive_weak(int pin)
{
    if (!STARK_HAL_GPIO_IN_RANGE(pin)) {
        return STARK_ERR_INVALID_ARG;
    }
    gpio_drive_cap_t cap = GPIO_DRIVE_CAP_3;
    esp_err_t err = gpio_set_drive_capability((gpio_num_t)pin, GPIO_DRIVE_CAP_0);
    if (err == ESP_OK) {
        err = gpio_get_drive_capability((gpio_num_t)pin, &cap);
    }
    if (err != ESP_OK || cap != GPIO_DRIVE_CAP_0) {
        return err != ESP_OK ? stark_err_from_esp(err) : STARK_ERR_IO;
    }
    return STARK_OK;
}

stark_err_t stark_hal_gpio_config_out_weak(int pin, bool initial)
{
    if (!STARK_HAL_GPIO_IN_RANGE(pin)) {
        return STARK_ERR_INVALID_ARG;
    }
    /* NOTE: the weakest strength and the level are in place before the output is enabled, so
     * the pad never drives harder or to another level than the caller asked for. */
    esp_err_t err = gpio_set_drive_capability((gpio_num_t)pin, GPIO_DRIVE_CAP_0);
    if (err == ESP_OK) {
        err = gpio_set_level((gpio_num_t)pin, initial ? 1 : 0);
    }
    if (err == ESP_OK && ((s_lab_out_mask >> pin) & 1u) == 0u) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << pin,
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        err = gpio_config(&cfg);
        if (err == ESP_OK) {
            s_lab_out_mask |= 1ULL << pin;
        }
    }
    gpio_drive_cap_t cap = GPIO_DRIVE_CAP_3;
    if (err == ESP_OK) {
        err = gpio_get_drive_capability((gpio_num_t)pin, &cap);
    }
    if (err != ESP_OK || cap != GPIO_DRIVE_CAP_0) {
        (void)stark_hal_gpio_hiz(pin);
        return err != ESP_OK ? stark_err_from_esp(err) : STARK_ERR_IO;
    }
    return STARK_OK;
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
    if (STARK_HAL_GPIO_IN_RANGE(pin)) {
        lab_release_output(pin); /* LEDC refuses a pad that was an output (see the NOTE above) */
    }
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
    esp_err_t cerr = ledc_channel_config(&ch_cfg);
    if (cerr == ESP_OK && STARK_HAL_GPIO_IN_RANGE(pin)) {
        s_lab_out_mask |= 1ULL << pin; /* LEDC reserved the pad: the next lab call resets it */
    }
    return stark_err_from_esp(cerr);
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
stark_err_t stark_hal_pwm_stop(uint8_t channel)
{
    return stark_err_from_esp(ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)channel, 0));
}
