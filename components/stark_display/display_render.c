/*
 * display_render.c — stark_display_render(): band rendering with two
 * ping-ponged DMA buffers (TASKS.md STARK-0016, ARCHITECTURE.md §6.5).
 * Target-only; the band arithmetic is display_bands.c (host-tested).
 *
 * Band i renders into buf[i % 2] while band i-1 is still on the wire. A
 * buffer is reused only after its previous transfer finished: the SPI
 * queue completes in submission order, so with both buffers in flight one
 * completion always frees the buffer the next band needs.
 *
 * Only the UI task renders (ARCHITECTURE §8); this is not re-entrant.
 */
#include "stark_display.h"
#include <stddef.h>
#include "display_bands.h"
#include "display_internal.h"
#include "stark_hal.h"
#include "stark_hal_esp.h"
#include "stark_log.h"

#define DISPLAY_FLUSH_TIMEOUT pdMS_TO_TICKS(1000)

/* stark_gfx stores native-endian RGB565; the ILI9341 wants it big-endian
 * on the wire and the 2.1.0 driver does not swap (STARK-0015). */
static void to_panel_order(uint16_t *px, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        px[i] = __builtin_bswap16(px[i]);
    }
}

static bool wait_one_transfer(void)
{
    return xSemaphoreTake(display_state.flush_done, DISPLAY_FLUSH_TIMEOUT) == pdTRUE;
}

static bool is_full_screen(const gfx_rect_t *bands, size_t n)
{
    return n > 0 && bands[0].x == 0 && bands[0].y == 0 && bands[0].w == DISPLAY_W &&
           bands[n - 1].y + bands[n - 1].h == DISPLAY_H;
}

stark_err_t stark_display_render(gfx_rect_t area, stark_render_fn fn, void *ctx)
{
    if (!display_state.ready) {
        return STARK_ERR_STATE;
    }
    if (fn == NULL) {
        return STARK_ERR_INVALID_ARG;
    }

    gfx_rect_t bands[DISPLAY_NUM_BANDS];
    size_t n = display_bands(area, DISPLAY_W, DISPLAY_H, DISPLAY_BAND_H, bands, DISPLAY_NUM_BANDS);
    uint64_t start_us = stark_hal_now_us();

    stark_err_t err = STARK_OK;
    int in_flight = 0;
    for (size_t i = 0; i < n; i++) {
        uint16_t *buf = display_state.buf[i % DISPLAY_NUM_BUFS];
        if (in_flight == DISPLAY_NUM_BUFS) {
            if (!wait_one_transfer()) { /* band i-2, the one holding buf */
                err = STARK_ERR_TIMEOUT;
                break;
            }
            in_flight--;
        }

        const gfx_rect_t *b = &bands[i];
        gfx_surface_t surface;
        gfx_surface_init(&surface, buf, b->w, b->h, b->x, b->y);
        fn(&surface, ctx);
        to_panel_order(buf, (size_t)b->w * (size_t)b->h);

        err = stark_err_from_esp(esp_lcd_panel_draw_bitmap(display_state.panel, b->x, b->y,
                                                           b->x + b->w, b->y + b->h, buf));
        if (err != STARK_OK) {
            break;
        }
        in_flight++;
    }

    /* Every buffer must be back before returning: the next call reuses them. */
    for (; in_flight > 0; in_flight--) {
        if (!wait_one_transfer() && err == STARK_OK) {
            err = STARK_ERR_TIMEOUT;
        }
    }

    if (err == STARK_OK && is_full_screen(bands, n)) {
        /* Informational only (ADR-0011): never a gate in Wokwi. */
        STARK_LOGI("display", "full refresh %u ms",
                   (unsigned)((stark_hal_now_us() - start_us) / 1000u));
    }
    return err;
}
