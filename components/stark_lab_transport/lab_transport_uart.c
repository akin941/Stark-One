/*
 * lab_transport_uart.c — the lab transport's UART group on ESP-IDF's UART driver
 * (stark_lab_transport.h, STARK-0705, ADR-0023 §7). Target-only: the host tests reach the same
 * guard through test/host/support/lab_transport_uart_host.c, the emulator test image through its
 * own transport.
 *
 * UART1 on stark_board's two lab pins, installed without a driver task (ESP-IDF's UART driver is
 * interrupt driven). The driver is given a small event queue because that is the only place it
 * reports framing and parity errors and overruns; every read drains the queue into the counters the
 * caller asked for. TX is routed to its pad only when the caller says so (`tx`): a monitor opens
 * the driver for RX and the TX pad is never touched. Close removes the driver and returns both pads
 * to hi-Z through stark_hal. The pins are named by stark_board and checked against its offer on
 * every open (stark_lab_uart_pins_ok()).
 */
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "stark_board.h"
#include "stark_hal.h"
#include "stark_lab_transport.h"

#define UART_PORT   UART_NUM_1
#define EVENT_DEPTH 32 /* events the driver can queue between two reads */

static bool s_installed;
static bool s_tx;
static QueueHandle_t s_events;

static const struct {
    uart_word_length_t data;
    uart_parity_t parity;
    uart_stop_bits_t stop;
} k_fmt[STARK_LAB_UART_FORMATS] = {
    {UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1}, /* 8N1 */
    {UART_DATA_8_BITS, UART_PARITY_EVEN, UART_STOP_BITS_1},    /* 8E1 */
    {UART_DATA_8_BITS, UART_PARITY_ODD, UART_STOP_BITS_1},     /* 8O1 */
    {UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_2}, /* 8N2 */
    {UART_DATA_7_BITS, UART_PARITY_EVEN, UART_STOP_BITS_1},    /* 7E1 */
};

/* Counts what the driver queued since the last look: errors, never data. */
static void drain_events(stark_lab_uart_err_t *errs)
{
    uart_event_t ev;
    while (s_events != NULL && xQueueReceive(s_events, &ev, 0) == pdTRUE) {
        if (errs == NULL) {
            continue;
        }
        switch (ev.type) {
            case UART_FRAME_ERR:
                errs->framing++;
                break;
            case UART_PARITY_ERR:
                errs->parity++;
                break;
            case UART_FIFO_OVF:
            case UART_BUFFER_FULL:
                errs->overrun++;
                break;
            case UART_DATA:
            case UART_BREAK:
            case UART_PATTERN_DET:
            case UART_DATA_BREAK:
            case UART_EVENT_MAX:
            default:
                break;
        }
    }
}

static void pads_hiz(void)
{
    const stark_board_pins_t *pins = stark_board_pins();
    (void)stark_hal_gpio_hiz(pins->lab_uart_tx);
    (void)stark_hal_gpio_hiz(pins->lab_uart_rx);
}

stark_err_t stark_lab_transport_uart_open(uint32_t baud, uint8_t fmt, size_t rx_buf, bool tx)
{
    if (!stark_lab_uart_args_ok(baud, fmt, rx_buf)) {
        return STARK_ERR_INVALID_ARG;
    }
    const stark_board_pins_t *pins = stark_board_pins();
    if (!stark_lab_uart_pins_ok(pins)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (s_installed) {
        return STARK_ERR_STATE;
    }
    const uart_config_t cfg = {.baud_rate = (int)baud,
                               .data_bits = k_fmt[fmt].data,
                               .parity = k_fmt[fmt].parity,
                               .stop_bits = k_fmt[fmt].stop,
                               .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                               .source_clk = UART_SCLK_DEFAULT};
    esp_err_t err = uart_driver_install(UART_PORT, (int)rx_buf, 0, EVENT_DEPTH, &s_events, 0);
    if (err != ESP_OK) {
        s_events = NULL;
        return err == ESP_ERR_NO_MEM ? STARK_ERR_NO_MEM : STARK_ERR_IO;
    }
    err = uart_param_config(UART_PORT, &cfg);
    if (err == ESP_OK) {
        err = uart_set_pin(UART_PORT, tx ? pins->lab_uart_tx : UART_PIN_NO_CHANGE,
                           pins->lab_uart_rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK && tx) {
        /* ADR-0023 §3: every lab output is driven at the weakest strength. */
        err = stark_hal_gpio_drive_weak(pins->lab_uart_tx) == STARK_OK ? ESP_OK : ESP_FAIL;
    }
    if (err != ESP_OK) {
        (void)uart_driver_delete(UART_PORT);
        s_events = NULL;
        pads_hiz();
        return STARK_ERR_IO;
    }
    s_installed = true;
    s_tx = tx;
    return STARK_OK;
}

int stark_lab_transport_uart_read(uint8_t *buf, size_t max, uint32_t timeout_ms,
                                  stark_lab_uart_err_t *errs)
{
    if (errs != NULL) {
        *errs = (stark_lab_uart_err_t){0};
    }
    if (buf == NULL || max == 0u || max > STARK_LAB_UART_READ_MAX ||
        timeout_ms > STARK_LAB_UART_TIMEOUT_MS_MAX) {
        return -(int)STARK_ERR_INVALID_ARG;
    }
    if (!s_installed) {
        return -(int)STARK_ERR_STATE;
    }
    drain_events(errs); /* what came since the previous read */
    int n = uart_read_bytes(UART_PORT, buf, (uint32_t)max, pdMS_TO_TICKS(timeout_ms));
    drain_events(errs);
    return n < 0 ? -(int)STARK_ERR_IO : n;
}

int stark_lab_transport_uart_write(const uint8_t *buf, size_t n, uint32_t timeout_ms)
{
    if (buf == NULL || n == 0u || n > STARK_LAB_UART_WRITE_MAX ||
        timeout_ms > STARK_LAB_UART_TIMEOUT_MS_MAX) {
        return -(int)STARK_ERR_INVALID_ARG;
    }
    if (!s_installed || !s_tx) {
        return -(int)STARK_ERR_STATE;
    }
    int w = uart_write_bytes(UART_PORT, buf, n);
    if (w < 0) {
        return -(int)STARK_ERR_IO;
    }
    esp_err_t err = uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(timeout_ms));
    if (err == ESP_ERR_TIMEOUT) {
        return -(int)STARK_ERR_TIMEOUT;
    }
    return err == ESP_OK ? w : -(int)STARK_ERR_IO;
}

stark_err_t stark_lab_transport_uart_set_baud(uint32_t baud)
{
    if (baud < STARK_LAB_UART_BAUD_MIN || baud > STARK_LAB_UART_BAUD_MAX) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!s_installed) {
        return STARK_ERR_STATE;
    }
    return uart_set_baudrate(UART_PORT, baud) == ESP_OK ? STARK_OK : STARK_ERR_IO;
}

void stark_lab_transport_uart_close(void)
{
    if (s_installed) {
        (void)uart_driver_delete(UART_PORT);
        s_installed = false;
        s_tx = false;
        s_events = NULL;
    }
    pads_hiz(); /* also after a failed open: nothing may stay routed to a pad */
}
