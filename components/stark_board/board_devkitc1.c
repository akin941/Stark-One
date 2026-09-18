/*
 * board_devkitc1.c — ESP32-S3-DevKitC-1 pin map and board init.
 * Pin values are normative per HARDWARE.md §2 — this is their only home
 * in the firmware (AGENTS.md).
 *
 * L0: depends only on ESP-IDF and stark_err.h (freestanding — see
 * ARCHITECTURE.md §2). Deliberately does not use stark_log: this file logs
 * nothing itself, it only returns stark_err_t; the caller (main.c) decides
 * what to log and what "a wiring mistake must not be subtle" (TASKS.md
 * STARK-0006) means in practice, since no panic/halt facility has an
 * owner yet.
 */
#include "stark_board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

/*
 * CONFIG_STARK_DISPLAY_BAND_H / CONFIG_STARK_DISPLAY_SPI_HZ are declared in
 * this component's Kconfig (see Kconfig for why: ARCHITECTURE.md §9 names
 * them as stark_display's, but stark_display does not exist yet and
 * spi_bus_initialize() below genuinely needs a byte count now).
 */
#define STARK_BOARD_SPI_MAX_TRANSFER_SZ (320 * CONFIG_STARK_DISPLAY_BAND_H * 2)

static const stark_board_pins_t s_pins = {
    .sclk = 12,
    .mosi = 11,
    .miso = 13,
    .tft_cs = 10,
    .tft_dc = 9,
    .tft_rst = 14,
    .tft_bl = 21,
    .key = {4, 5, 6, 7, 15, 16}, /* UP DOWN LEFT RIGHT OK BACK */
    .buzzer = 17,
    .led_status = 18,
    .sd_cs = 8,
    .i2c_sda = 1,
    .i2c_scl = 2,
    .tft_spi_hz = CONFIG_STARK_DISPLAY_SPI_HZ,
};

const stark_board_pins_t *stark_board_pins(void)
{
    return &s_pins;
}

const char *stark_board_name(void)
{
    return "devkitc1";
}

stark_err_t stark_board_init(void)
{
    stark_err_t verr = stark_board_validate(&s_pins);
    if (verr != STARK_OK) {
        return verr;
    }

    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << s_pins.led_status,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&led_cfg);
    if (err != ESP_OK) {
        return STARK_ERR_IO;
    }

    uint64_t key_mask = 0;
    for (int i = 0; i < STARK_KEY_COUNT; i++) {
        key_mask |= (1ULL << s_pins.key[i]);
    }
    gpio_config_t key_cfg = {
        .pin_bit_mask = key_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&key_cfg);
    if (err != ESP_OK) {
        return STARK_ERR_IO;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = s_pins.mosi,
        .miso_io_num = s_pins.miso,
        .sclk_io_num = s_pins.sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = STARK_BOARD_SPI_MAX_TRANSFER_SZ,
    };
    err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return STARK_ERR_IO;
    }

    return STARK_OK;
}
