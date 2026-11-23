/*
 * stark_subghz.c — the Sub-GHz service's target port (stark_subghz.h): binds
 * the pure service (subghz_svc.c) to stark_module's claims, the transport and
 * FreeRTOS time, and runs the service task. Every transport call after init is
 * made by that task. The requests (meter, scan, stop, status) and the task's own
 * steps are serialised by one mutex; a step holds it for at most one channel or one
 * identification, so a caller waits milliseconds. Target-only.
 */
#include "stark_subghz.h"
#include <stdatomic.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "stark_event.h"
#include "stark_log.h"
#include "stark_module.h"
#include "stark_subghz_transport.h"
#include "subghz_svc.h"

#define SUBGHZ_CORE 0 /* the UI task runs on core 1 */

static subghz_svc_t s_svc;
static atomic_bool s_ready;
static StackType_t s_stack[CONFIG_STARK_SUBGHZ_TASK_STACK / sizeof(StackType_t)];
static StaticTask_t s_tcb;
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;

static size_t io_list(void *ctx, stark_module_entry_t *out, size_t max)
{
    (void)ctx;
    return stark_module_list(out, max);
}

static stark_err_t io_claim(void *ctx, uint8_t slot, stark_module_lease_t *out)
{
    (void)ctx;
    /* NOTE: a claim names lines, not capabilities; the radio bit was checked by find_slot(). */
    return stark_module_claim(slot, STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ, "subghz", out);
}

static void io_release(void *ctx, const stark_module_lease_t *lease)
{
    (void)ctx;
    stark_module_release(lease);
}

static bool io_lease_valid(void *ctx, const stark_module_lease_t *lease)
{
    (void)ctx;
    return stark_module_lease_valid(lease);
}

static stark_err_t io_open(void *ctx, const stark_module_lease_t *lease)
{
    (void)ctx;
    return stark_subghz_transport_open(lease);
}

static void io_close(void *ctx)
{
    (void)ctx;
    stark_subghz_transport_close();
}

static stark_err_t io_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    (void)ctx;
    return stark_subghz_transport_xfer(tx, rx, len);
}

static stark_err_t io_rx_start(void *ctx)
{
    (void)ctx;
    return stark_subghz_transport_rx_start();
}

static size_t io_rx_take(void *ctx, subghz_sym_t *out, size_t max, uint32_t timeout_ms)
{
    (void)ctx;
    return stark_subghz_transport_rx_take(out, max, timeout_ms);
}

static void io_rx_stop(void *ctx)
{
    (void)ctx;
    stark_subghz_transport_rx_stop();
}

static uint32_t io_now(void *ctx)
{
    (void)ctx;
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void io_delay(void *ctx, uint32_t ms)
{
    (void)ctx;
    /* NOTE: one tick more, so a wait is never shorter than asked (the 1 ms scan settle) */
    vTaskDelay(pdMS_TO_TICKS(ms) + 1);
}

static void io_log(void *ctx, subghz_log_t what, unsigned a, unsigned b)
{
    (void)ctx;
    char mhz[12];
    char dbm[12];
    switch (what) {
        case SUBGHZ_LOG_NO_MODULE:
            STARK_LOGI("subghz", "no module");
            break;
        case SUBGHZ_LOG_VERSION:
            STARK_LOGI("subghz", "cc1101 version 0x%02x", a);
            break;
        case SUBGHZ_LOG_NOT_CC1101:
            STARK_LOGW("subghz", "not a cc1101 (partnum 0x%02x version 0x%02x)", a, b);
            break;
        case SUBGHZ_LOG_LOST:
            STARK_LOGW("subghz", "module lost");
            break;
        case SUBGHZ_LOG_METER:
            subghz_mhz_text(mhz, sizeof mhz, a);
            STARK_LOGI("subghz", "meter %s MHz", mhz);
            break;
        case SUBGHZ_LOG_RSSI:
            subghz_dbm_text(dbm, sizeof dbm, (int16_t)(int)a);
            STARK_LOGI("subghz", "rssi %s dBm", dbm);
            break;
        case SUBGHZ_LOG_PEAK:
            subghz_mhz_text(mhz, sizeof mhz, a);
            subghz_dbm_text(dbm, sizeof dbm, (int16_t)(int)b);
            STARK_LOGI("subghz", "peak %s MHz %s dBm", mhz, dbm);
            break;
        case SUBGHZ_LOG_IDLE:
            STARK_LOGI("subghz", "idle");
            break;
        case SUBGHZ_LOG_CAPTURE_START:
            subghz_mhz_text(mhz, sizeof mhz, a);
            STARK_LOGI("subghz", "capture start %s MHz %s", mhz, subghz_mod_name((subghz_mod_t)b));
            break;
        case SUBGHZ_LOG_CAPTURE_DONE:
            STARK_LOGI("subghz", "capture %u sym hash 0x%08x", a, b);
            break;
        case SUBGHZ_LOG_ERROR:
        default:
            STARK_LOGW("subghz", "module not usable: %s", stark_err_str((stark_err_t)a));
            break;
    }
}

static void io_changed(void *ctx, bool ready)
{
    (void)ctx;
    atomic_store(&s_ready, ready);
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_SUBGHZ;
    e.system.arg = STARK_SUBGHZ_EVT_MODULE;
    (void)stark_event_publish(&e);
}

static void io_notify(void *ctx, int evt)
{
    (void)ctx;
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_SUBGHZ;
    e.system.arg = evt;
    (void)stark_event_publish(&e);
}

static const subghz_svc_io_t k_io = {
    io_list,    io_claim,   io_release, io_lease_valid, io_open, io_close,   io_xfer,   io_rx_start,
    io_rx_take, io_rx_stop, io_now,     io_delay,       io_log,  io_changed, io_notify, NULL};

static void subghz_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        uint32_t wait_ms = subghz_svc_step(&s_svc);
        xSemaphoreGive(s_lock);
        if (wait_ms > CONFIG_STARK_SUBGHZ_POLL_MS) {
            wait_ms = CONFIG_STARK_SUBGHZ_POLL_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

stark_err_t stark_subghz_init(void)
{
    subghz_svc_init(&s_svc, &k_io);
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    if (xTaskCreateStaticPinnedToCore(
            subghz_task, "stark_subghz", (uint32_t)(sizeof s_stack / sizeof s_stack[0]), NULL,
            CONFIG_STARK_SUBGHZ_TASK_PRIO, s_stack, &s_tcb, SUBGHZ_CORE) == NULL) {
        return STARK_ERR_NO_MEM;
    }
    return STARK_OK;
}

bool stark_subghz_ready(void)
{
    return atomic_load(&s_ready);
}

stark_err_t stark_subghz_meter_start(uint32_t hz)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    stark_err_t err = subghz_svc_meter(&s_svc, hz);
    xSemaphoreGive(s_lock);
    return err;
}

stark_err_t stark_subghz_scan_start(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    stark_err_t err = subghz_svc_scan(&s_svc);
    xSemaphoreGive(s_lock);
    return err;
}

stark_err_t stark_subghz_capture_start(subghz_mod_t mod, uint32_t hz, subghz_sym_t *buf, size_t cap)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    stark_err_t err = subghz_svc_capture(&s_svc, mod, hz, buf, cap);
    xSemaphoreGive(s_lock);
    return err;
}

void stark_subghz_capture_finish(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    subghz_svc_capture_finish(&s_svc);
    xSemaphoreGive(s_lock);
}

void stark_subghz_stop(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    subghz_svc_stop(&s_svc);
    xSemaphoreGive(s_lock);
}

void stark_subghz_status(stark_subghz_status_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    subghz_svc_status(&s_svc, out);
    xSemaphoreGive(s_lock);
}
