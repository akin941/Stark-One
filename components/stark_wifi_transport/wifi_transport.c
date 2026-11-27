/*
 * wifi_transport.c — the production Wi-Fi transport (STARK-1005, ADR-0027,
 * ADR-0028): the only esp_wifi caller. Promiscuous receive into a bounded
 * queue; the service drains it. Receive-only — there is no transmit path here
 * (ADR-0027 §3). Opened lazily by the service, never at boot (ADR-0028), so the
 * unmodelled RF path is not exercised in the emulator; the test image swaps
 * this file for a fake. Target-only.
 */
#include "stark_wifi_transport.h"

#include <string.h>

#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* A received frame handed from the promiscuous callback to the service task. */
typedef struct {
    uint8_t len;
    int8_t rssi;
    uint8_t channel;
    uint8_t data[STARK_WIFI_FRAME_MAX];
} wifi_rx_rec_t;

#define WIFI_RX_QUEUE_LEN 16u

static QueueHandle_t s_queue;
static bool s_open;

/* Runs in the Wi-Fi task context (not an ISR). Drops frames when the queue is
 * full — a survey tolerates loss; it never blocks the radio. */
static void promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (buf == NULL || s_queue == NULL) {
        return;
    }
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA && type != WIFI_PKT_CTRL) {
        return;
    }
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    uint32_t n = (uint32_t)pkt->rx_ctrl.sig_len;
    if (n == 0u) {
        return;
    }
    if (n > STARK_WIFI_FRAME_MAX) {
        n = STARK_WIFI_FRAME_MAX;
    }
    wifi_rx_rec_t rec;
    rec.len = (uint8_t)n;
    rec.rssi = (int8_t)pkt->rx_ctrl.rssi;
    rec.channel = (uint8_t)pkt->rx_ctrl.channel;
    memcpy(rec.data, pkt->payload, n);
    (void)xQueueSend(s_queue, &rec, 0);
}

stark_err_t stark_wifi_transport_open(void)
{
    if (s_open) {
        return STARK_OK;
    }
    if (s_queue == NULL) {
        s_queue = xQueueCreate(WIFI_RX_QUEUE_LEN, sizeof(wifi_rx_rec_t));
        if (s_queue == NULL) {
            return STARK_ERR_NO_MEM;
        }
    }
    /* esp_wifi needs the default event loop; tolerate one already created. */
    esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        return STARK_ERR_IO;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    e = esp_wifi_init(&cfg);
    if (e != ESP_OK) {
        return STARK_ERR_IO;
    }
    (void)esp_wifi_set_storage(WIFI_STORAGE_RAM);
    (void)esp_wifi_set_mode(WIFI_MODE_NULL); /* no station, no AP, no association */
    e = esp_wifi_start();
    if (e != ESP_OK) {
        (void)esp_wifi_deinit();
        return STARK_ERR_IO;
    }
    (void)esp_wifi_set_promiscuous(true);
    (void)esp_wifi_set_promiscuous_rx_cb(promisc_cb);
    (void)esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    s_open = true;
    return STARK_OK;
}

void stark_wifi_transport_close(void)
{
    if (!s_open) {
        return;
    }
    (void)esp_wifi_set_promiscuous(false);
    (void)esp_wifi_stop();
    (void)esp_wifi_deinit();
    s_open = false;
    if (s_queue != NULL) {
        xQueueReset(s_queue);
    }
}

stark_err_t stark_wifi_transport_set_channel(uint8_t channel)
{
    if (channel < 1u || channel > 14u) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    esp_err_t e = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    return (e == ESP_OK) ? STARK_OK : STARK_ERR_IO;
}

size_t stark_wifi_transport_rx(uint8_t *buf, size_t cap, int8_t *rssi, uint8_t *channel,
                               uint32_t timeout_ms)
{
    if (!s_open || s_queue == NULL || cap == 0u || buf == NULL || rssi == NULL || channel == NULL) {
        return 0u;
    }
    wifi_rx_rec_t rec;
    if (xQueueReceive(s_queue, &rec, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return 0u;
    }
    size_t n = rec.len;
    if (n > cap) {
        n = cap;
    }
    memcpy(buf, rec.data, n);
    *rssi = rec.rssi;
    *channel = rec.channel;
    return n;
}
