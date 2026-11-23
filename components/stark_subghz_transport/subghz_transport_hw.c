/*
 * subghz_transport_hw.c — the CC1101 module over ESP-IDF (stark_subghz_transport.h):
 * SPI through stark_module_spi on the claimed slot, GDO2 through an RMT receive
 * channel. Target-only. The receive path is written like stark_ir's and, like
 * it, is exercised only on hardware: esp-emu has no radio model and its RMT
 * loopback is spoken for (ADR-0022 §8, docs/VALIDATION.md §2).
 *
 * NOTE: no GPIO number appears here. The pins come from the lease, which got
 * them from stark_board. GDO0 is not touched at all: the chip leaves it high
 * impedance in receive, and the transmit path belongs to STARK-0605.
 */
#include "driver/rmt_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include "stark_subghz_transport.h"

#define RESOLUTION_HZ 1000000u /* 1 tick = 1 us */
#define RX_SYMBOLS    64u      /* one capture's worth; the driver ping-pongs the hardware memory */
#define RX_MEM_BLOCK  64u
#define GLITCH_NS     2000u     /* shorter pulses are noise */
#define IDLE_NS       20000000u /* 20 ms of idle ends a burst */
#define SYM_MAX_US    1000000

static stark_module_lease_t s_lease;
static stark_module_spi_t *s_dev;
static int s_gdo2 = -1;

static rmt_channel_handle_t s_rx;
static bool s_rx_on;
static rmt_symbol_word_t s_rx_buf[RX_SYMBOLS];
static const rmt_receive_config_t k_rx_cfg = {
    .signal_range_min_ns = GLITCH_NS,
    .signal_range_max_ns = IDLE_NS,
};
static StaticQueue_t s_queue_buf;
static uint8_t s_queue_storage[2 * sizeof(uint16_t)];
static QueueHandle_t s_queue;

static bool on_recv_done(rmt_channel_handle_t chan, const rmt_rx_done_event_data_t *edata,
                         void *ctx)
{
    (void)chan;
    (void)ctx;
    uint16_t n = (uint16_t)edata->num_symbols;
    BaseType_t woken = pdFALSE;
    (void)xQueueSendFromISR(s_queue, &n, &woken);
    return woken == pdTRUE;
}

stark_err_t stark_subghz_transport_open(const stark_module_lease_t *lease)
{
    if (lease == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_dev != NULL) {
        return STARK_ERR_BUSY;
    }
    int cs;
    int gdo0;
    int gdo2;
    stark_err_t err = stark_module_lease_pins(lease, &cs, &gdo0, &gdo2);
    if (err != STARK_OK) {
        return err;
    }
    err = stark_module_spi_attach(lease, CONFIG_STARK_SUBGHZ_SPI_HZ, 0, &s_dev);
    if (err != STARK_OK) {
        s_dev = NULL;
        return err;
    }
    s_lease = *lease;
    s_gdo2 = gdo2;
    return STARK_OK;
}

void stark_subghz_transport_close(void)
{
    stark_subghz_transport_rx_stop();
    if (s_dev != NULL) {
        stark_module_spi_detach(s_dev);
        s_dev = NULL;
    }
    s_gdo2 = -1;
}

stark_err_t stark_subghz_transport_xfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (s_dev == NULL) {
        return STARK_ERR_STATE;
    }
    return stark_module_spi_xfer(s_dev, tx, rx, len);
}

stark_err_t stark_subghz_transport_rx_start(void)
{
    if (s_dev == NULL || !stark_module_lease_valid(&s_lease)) {
        return STARK_ERR_STATE;
    }
    if (s_rx_on) {
        return STARK_OK;
    }
    if (s_queue == NULL) {
        s_queue = xQueueCreateStatic(2, sizeof(uint16_t), s_queue_storage, &s_queue_buf);
    }
    const rmt_rx_channel_config_t cfg = {
        .gpio_num = s_gdo2,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RESOLUTION_HZ,
        .mem_block_symbols = RX_MEM_BLOCK,
    };
    const rmt_rx_event_callbacks_t cbs = {.on_recv_done = on_recv_done};
    if (rmt_new_rx_channel(&cfg, &s_rx) != ESP_OK) {
        s_rx = NULL;
        return STARK_ERR_IO;
    }
    if (rmt_rx_register_event_callbacks(s_rx, &cbs, NULL) != ESP_OK || rmt_enable(s_rx) != ESP_OK) {
        (void)rmt_del_channel(s_rx);
        s_rx = NULL;
        return STARK_ERR_IO;
    }
    (void)xQueueReset(s_queue);
    s_rx_on = true;
    if (rmt_receive(s_rx, s_rx_buf, sizeof s_rx_buf, &k_rx_cfg) != ESP_OK) {
        stark_subghz_transport_rx_stop();
        return STARK_ERR_IO;
    }
    return STARK_OK;
}

/* Appends one RMT half-symbol; false when out is full. */
static bool put(subghz_sym_t *out, size_t max, size_t *n, uint32_t level, uint32_t ticks)
{
    if (ticks == 0) {
        return true; /* the end marker's empty half */
    }
    if (*n >= max) {
        return false;
    }
    subghz_sym_t us = ticks > SYM_MAX_US ? SYM_MAX_US : (subghz_sym_t)ticks;
    out[(*n)++] = level != 0 ? us : -us;
    return true;
}

size_t stark_subghz_transport_rx_take(subghz_sym_t *out, size_t max, uint32_t timeout_ms)
{
    uint16_t symbols = 0;
    if (!s_rx_on || out == NULL ||
        xQueueReceive(s_queue, &symbols, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return 0;
    }
    size_t n = 0;
    for (uint16_t i = 0; i < symbols && i < RX_SYMBOLS; i++) {
        const rmt_symbol_word_t *w = &s_rx_buf[i];
        if (!put(out, max, &n, w->level0, w->duration0) ||
            !put(out, max, &n, w->level1, w->duration1)) {
            break;
        }
    }
    if (s_rx_on) {
        (void)rmt_receive(s_rx, s_rx_buf, sizeof s_rx_buf, &k_rx_cfg); /* re-arm */
    }
    /* NOTE: a frame ends at the idle threshold, whose low time the RMT does not report; the
     * gap is part of the signal, and without it the next frame's first high would merge
     * into this one's last (subghz_cap_feed) */
    if (n > 0 && n < max && out[n - 1] > 0) {
        out[n++] = -(subghz_sym_t)(IDLE_NS / 1000u);
    }
    return n;
}

void stark_subghz_transport_rx_stop(void)
{
    if (s_rx_on) {
        s_rx_on = false;
        (void)rmt_disable(s_rx);
        (void)rmt_del_channel(s_rx);
        s_rx = NULL;
    }
}

stark_err_t stark_subghz_transport_tx(const subghz_sym_t *s, size_t n, uint32_t timeout_ms)
{
    (void)s;
    (void)n;
    (void)timeout_ms;
    return STARK_ERR_NOT_SUPPORTED;
}
