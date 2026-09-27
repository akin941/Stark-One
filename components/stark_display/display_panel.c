/*
 * display_panel.c — ILI9341 bring-up on SPI2 via esp_lcd + the registry
 * driver espressif/esp_lcd_ili9341 ==2.1.0 (TASKS.md STARK-0015, ADR-0004).
 * Target-only. Rendering is display_render.c (STARK-0016).
 *
 * The panel's RST and backlight (LEDC channel 1) are driven even though
 * Wokwi models neither (ADR-0010). All pins come from stark_board.
 */
#include "stark_display.h"
#include <stdbool.h>
#include <stddef.h>
#include "display_internal.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "stark_board.h"
#include "stark_hal.h"
#include "stark_hal_esp.h"
#include "stark_log.h"

#define DISPLAY_BACKLIGHT_CH 1 /* LEDC allocation: stark_hal.h */
#define DISPLAY_BACKLIGHT_HZ 5000

/* Kconfig bools are undefined when off; turn them into plain booleans once. */
#ifdef CONFIG_STARK_DISPLAY_SWAP_XY
#define DISPLAY_SWAP_XY true
#else
#define DISPLAY_SWAP_XY false
#endif
#ifdef CONFIG_STARK_DISPLAY_MIRROR_X
#define DISPLAY_MIRROR_X true
#else
#define DISPLAY_MIRROR_X false
#endif
#ifdef CONFIG_STARK_DISPLAY_MIRROR_Y
#define DISPLAY_MIRROR_Y true
#else
#define DISPLAY_MIRROR_Y false
#endif
#ifdef CONFIG_STARK_DISPLAY_BGR
#define DISPLAY_BGR true
#else
#define DISPLAY_BGR false
#endif
#ifdef CONFIG_STARK_DISPLAY_INVERT
#define DISPLAY_INVERT true
#else
#define DISPLAY_INVERT false
#endif

display_state_t display_state;

static esp_lcd_panel_io_handle_t s_io;
static StaticSemaphore_t s_flush_buf;

/* SPI transfer-done ISR: counts one finished band transfer for the renderer. */
static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata,
                          void *ctx)
{
    (void)io;
    (void)edata;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(display_state.flush_done, &woken);
    return woken == pdTRUE;
}

static void release(void)
{
    if (display_state.panel != NULL) {
        (void)esp_lcd_panel_del(display_state.panel);
        display_state.panel = NULL;
    }
    if (s_io != NULL) {
        (void)esp_lcd_panel_io_del(s_io);
        s_io = NULL;
    }
    for (int i = 0; i < DISPLAY_NUM_BUFS; i++) {
        heap_caps_free(display_state.buf[i]);
        display_state.buf[i] = NULL;
    }
}

static stark_err_t panel_setup(void)
{
    const stark_board_pins_t *pins = stark_board_pins();

    esp_lcd_panel_io_spi_config_t io_cfg =
        ILI9341_PANEL_IO_SPI_CONFIG(pins->tft_cs, pins->tft_dc, on_color_done, NULL);
    io_cfg.pclk_hz = pins->tft_spi_hz;
    STARK_CHECK_RET(stark_err_from_esp(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &s_io)));

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = pins->tft_rst,
        .rgb_ele_order = DISPLAY_BGR ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    esp_lcd_panel_handle_t panel = NULL;
    STARK_CHECK_RET(stark_err_from_esp(esp_lcd_new_panel_ili9341(s_io, &panel_cfg, &panel)));
    display_state.panel = panel;
    STARK_CHECK_RET(stark_err_from_esp(esp_lcd_panel_reset(panel)));
    STARK_CHECK_RET(stark_err_from_esp(esp_lcd_panel_init(panel)));
    STARK_CHECK_RET(stark_err_from_esp(esp_lcd_panel_invert_color(panel, DISPLAY_INVERT)));
    STARK_CHECK_RET(stark_err_from_esp(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY)));
    STARK_CHECK_RET(
        stark_err_from_esp(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y)));
    STARK_CHECK_RET(stark_err_from_esp(esp_lcd_panel_disp_on_off(panel, true)));

    if (pins->tft_bl >= 0) {
        STARK_CHECK_RET(
            stark_hal_pwm_init(pins->tft_bl, DISPLAY_BACKLIGHT_HZ, DISPLAY_BACKLIGHT_CH));
        STARK_CHECK_RET(stark_hal_pwm_set_duty_pct(DISPLAY_BACKLIGHT_CH, 100));
    }

    /* All display allocation happens here, at init (ARCHITECTURE §8). */
    for (int i = 0; i < DISPLAY_NUM_BUFS; i++) {
        display_state.buf[i] = heap_caps_malloc(DISPLAY_BAND_PIXELS * sizeof(uint16_t),
                                                MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (display_state.buf[i] == NULL) {
            return STARK_ERR_NO_MEM;
        }
    }
    return STARK_OK;
}

stark_err_t stark_display_init(void)
{
    if (display_state.ready) {
        return STARK_ERR_STATE;
    }
    display_state.flush_done = xSemaphoreCreateCountingStatic(DISPLAY_NUM_BUFS, 0, &s_flush_buf);
    stark_err_t err = panel_setup();
    if (err != STARK_OK) {
        release();
        return err;
    }
    display_state.ready = true;
    STARK_LOGI("display", "init %dx%d band=%d bufs=%dx%u", DISPLAY_W, DISPLAY_H, DISPLAY_BAND_H,
               DISPLAY_NUM_BUFS, (unsigned)(DISPLAY_BAND_PIXELS * sizeof(uint16_t)));
    return STARK_OK;
}

int16_t stark_display_width(void)
{
    return DISPLAY_W;
}

int16_t stark_display_height(void)
{
    return DISPLAY_H;
}

stark_err_t stark_display_set_backlight(uint8_t pct)
{
    if (!display_state.ready) {
        return STARK_ERR_STATE;
    }
    if (stark_board_pins()->tft_bl < 0) {
        return STARK_OK; /* no backlight on this board: nothing to dim */
    }
    return stark_hal_pwm_set_duty_pct(DISPLAY_BACKLIGHT_CH, pct);
}
