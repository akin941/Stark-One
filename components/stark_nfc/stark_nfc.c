/*
 * stark_nfc.c — the NFC service's target port (stark_nfc.h): binds the pure
 * PN532 driver to stark_nfc_transport and FreeRTOS time. Target-only.
 */
#include "stark_nfc.h"
#include <stdatomic.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pn532.h"
#include "sdkconfig.h"
#include "stark_log.h"
#include "stark_nfc_transport.h"

static pn532_t s_pn532;
static _Atomic int s_module = STARK_NFC_NOT_FITTED;

static stark_err_t io_write(void *ctx, const uint8_t *buf, size_t len)
{
    (void)ctx;
    return stark_nfc_transport_write(buf, len, CONFIG_STARK_NFC_I2C_TIMEOUT_MS);
}

static stark_err_t io_read(void *ctx, uint8_t *buf, size_t len)
{
    (void)ctx;
    return stark_nfc_transport_read(buf, len, CONFIG_STARK_NFC_I2C_TIMEOUT_MS);
}

static uint32_t io_now(void *ctx)
{
    (void)ctx;
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void io_delay(void *ctx, uint32_t ms)
{
    (void)ctx;
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks > 0 ? ticks : 1);
}

static const pn532_io_t k_io = {io_write, io_read, io_now, io_delay, NULL};

stark_err_t stark_nfc_init(void)
{
    stark_err_t err = stark_nfc_transport_open();
    if (err == STARK_ERR_NOT_SUPPORTED) {
        atomic_store(&s_module, STARK_NFC_NOT_FITTED);
        STARK_LOGI("nfc", "not fitted");
        return STARK_OK;
    }
    if (err != STARK_OK) {
        atomic_store(&s_module, STARK_NFC_ABSENT);
        STARK_LOGW("nfc", "no module (%s)", stark_err_str(err));
        return STARK_OK;
    }
    pn532_bind(&s_pn532, &k_io);
    uint8_t ic = 0;
    uint8_t ver = 0;
    uint8_t rev = 0;
    err = pn532_detect(&s_pn532, &ic, &ver, &rev);
    if (err == STARK_OK) {
        atomic_store(&s_module, STARK_NFC_READY);
        STARK_LOGI("nfc", "pn532 fw %u.%u", (unsigned)ver, (unsigned)rev);
    } else if (err == STARK_ERR_NOT_SUPPORTED) {
        atomic_store(&s_module, STARK_NFC_ABSENT);
        STARK_LOGW("nfc", "unexpected chip 0x%02x", (unsigned)ic);
    } else {
        atomic_store(&s_module, STARK_NFC_ABSENT);
        STARK_LOGI("nfc", "no module (%s)", stark_err_str(err));
    }
    return STARK_OK;
}

stark_nfc_module_t stark_nfc_module(void)
{
    return (stark_nfc_module_t)atomic_load(&s_module);
}
