/*
 * display_panel.c — ILI9341 bring-up on SPI2 via esp_lcd + the registry
 * driver espressif/esp_lcd_ili9341 ==2.1.0 (TASKS.md STARK-0015, ADR-0004).
 * Target-only.
 *
 * The panel's RST and backlight (LEDC channel 1) are driven even though
 * Wokwi models neither (ADR-0010). All pins come from stark_board.
 */
#include "stark_display.h"
#include <stdbool.h>
#include <stddef.h>
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "stark_board.h"
#include "stark_gfx.h"
#include "stark_hal.h"
#include "stark_hal_esp.h"

#define DISPLAY_W             320
#define DISPLAY_H             240
#define DISPLAY_BAND_H        CONFIG_STARK_DISPLAY_BAND_H
#define DISPLAY_BACKLIGHT_CH  1 /* LEDC allocation: stark_hal.h */
#define DISPLAY_BACKLIGHT_HZ  5000
#define DISPLAY_FLUSH_TIMEOUT pdMS_TO_TICKS(1000)

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

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static StaticSemaphore_t s_flush_buf;
static SemaphoreHandle_t s_flush_done;
static uint16_t *s_band; /* DISPLAY_W x DISPLAY_BAND_H, DMA-capable */
static bool s_ready;

/* SPI transfer-done ISR: hands the band buffer back to the drawing task. */
static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata,
                          void *ctx)
{
    (void)io;
    (void)edata;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &woken);
    return woken == pdTRUE;
}

static void release(void)
{
    if (s_panel != NULL) {
        (void)esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_io != NULL) {
        (void)esp_lcd_panel_io_del(s_io);
        s_io = NULL;
    }
    heap_caps_free(s_band);
    s_band = NULL;
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
    STARK_CHECK_RET(stark_err_from_esp(esp_lcd_new_panel_ili9341(s_io, &panel_cfg, &s_panel)));
    STARK_CHECK_RET(stark_err_from_esp(esp_lcd_panel_reset(s_panel)));
    STARK_CHECK_RET(stark_err_from_esp(esp_lcd_panel_init(s_panel)));
    STARK_CHECK_RET(stark_err_from_esp(esp_lcd_panel_invert_color(s_panel, DISPLAY_INVERT)));
    STARK_CHECK_RET(stark_err_from_esp(esp_lcd_panel_swap_xy(s_panel, DISPLAY_SWAP_XY)));
    STARK_CHECK_RET(
        stark_err_from_esp(esp_lcd_panel_mirror(s_panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y)));
    STARK_CHECK_RET(stark_err_from_esp(esp_lcd_panel_disp_on_off(s_panel, true)));

    if (pins->tft_bl >= 0) {
        STARK_CHECK_RET(
            stark_hal_pwm_init(pins->tft_bl, DISPLAY_BACKLIGHT_HZ, DISPLAY_BACKLIGHT_CH));
        STARK_CHECK_RET(stark_hal_pwm_set_duty_pct(DISPLAY_BACKLIGHT_CH, 100));
    }

    s_band = heap_caps_malloc((size_t)DISPLAY_W * DISPLAY_BAND_H * sizeof(uint16_t),
                              MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    return s_band != NULL ? STARK_OK : STARK_ERR_NO_MEM;
}

stark_err_t stark_display_init(void)
{
    if (s_ready) {
        return STARK_ERR_STATE;
    }
    s_flush_done = xSemaphoreCreateBinaryStatic(&s_flush_buf);
    stark_err_t err = panel_setup();
    if (err != STARK_OK) {
        release();
        return err;
    }
    s_ready = true;
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
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    if (stark_board_pins()->tft_bl < 0) {
        return STARK_OK; /* no backlight on this board: nothing to dim */
    }
    return stark_hal_pwm_set_duty_pct(DISPLAY_BACKLIGHT_CH, pct);
}

/* stark_gfx stores native-endian RGB565; the ILI9341 wants it big-endian
 * on the wire and the 2.1.0 driver does not swap (TASKS.md: the panel-side
 * byte order is this component's job). */
static void to_panel_order(uint16_t *px, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        px[i] = __builtin_bswap16(px[i]);
    }
}

static stark_err_t flush_band(int y0, int h)
{
    to_panel_order(s_band, (size_t)DISPLAY_W * (size_t)h);
    STARK_CHECK_RET(
        stark_err_from_esp(esp_lcd_panel_draw_bitmap(s_panel, 0, y0, DISPLAY_W, y0 + h, s_band)));
    return xSemaphoreTake(s_flush_done, DISPLAY_FLUSH_TIMEOUT) == pdTRUE ? STARK_OK
                                                                         : STARK_ERR_TIMEOUT;
}

static void draw_test_pattern(gfx_surface_t *s)
{
    static const uint16_t bars[8] = {
        GFX_RGB565(255, 0, 0),     GFX_RGB565(0, 255, 0),   GFX_RGB565(0, 0, 255),
        GFX_RGB565(255, 255, 0),   GFX_RGB565(0, 255, 255), GFX_RGB565(255, 0, 255),
        GFX_RGB565(255, 255, 255), GFX_RGB565(0, 0, 0),
    };
    const int16_t bar_w = DISPLAY_W / 8;
    for (int16_t i = 0; i < 8; i++) {
        gfx_fill(s, (gfx_rect_t){(int16_t)(i * bar_w), 0, bar_w, DISPLAY_H}, bars[i]);
    }
    const uint16_t white = GFX_RGB565(255, 255, 255);
    gfx_rect(s, (gfx_rect_t){0, 0, DISPLAY_W, DISPLAY_H}, white);
    gfx_fill(s, (gfx_rect_t){2, 2, 12, 12}, white); /* orientation marker: top-left */
}

stark_err_t stark_display_test_pattern(void)
{
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    for (int y0 = 0; y0 < DISPLAY_H; y0 += DISPLAY_BAND_H) {
        int h = DISPLAY_H - y0 < DISPLAY_BAND_H ? DISPLAY_H - y0 : DISPLAY_BAND_H;
        gfx_surface_t band;
        gfx_surface_init(&band, s_band, DISPLAY_W, (int16_t)h, 0, (int16_t)y0);
        draw_test_pattern(&band);
        STARK_CHECK_RET(flush_band(y0, h));
    }
    return STARK_OK;
}
