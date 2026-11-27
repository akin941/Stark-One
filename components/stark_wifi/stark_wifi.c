/*
 * stark_wifi.c — the Wi-Fi analysis service's target port (stark_wifi.h,
 * STARK-1005, ADR-0028): binds the pure cores (wifi_svc) to the esp_wifi
 * transport and FreeRTOS, and runs the service task. The radio is opened lazily
 * by stark_wifi_start() (the analysis app), never at boot, so the emulator never
 * runs the unmodelled RF path. Receive-only; no transmit path exists. Target-only.
 */
#include "stark_wifi.h"

#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "stark_event.h"
#include "stark_log.h"
#include "stark_wifi_transport.h"
#include "wifi_svc.h"

#define WIFI_CORE 0 /* the UI task runs on core 1 */

static wifi_svc_t s_svc;
static atomic_bool s_ready;
static atomic_bool s_run;
static StackType_t s_stack[CONFIG_STARK_WIFI_TASK_STACK / sizeof(StackType_t)];
static StaticTask_t s_tcb;
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;

static void publish_module(void)
{
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_WIFI;
    e.system.arg = STARK_WIFI_EVT_MODULE;
    (void)stark_event_publish(&e);
}

static void wifi_task(void *arg)
{
    (void)arg;
    uint8_t buf[STARK_WIFI_FRAME_MAX];
    for (;;) {
        if (!atomic_load(&s_run)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int8_t rssi = 0;
        uint8_t channel = 0;
        size_t n =
            stark_wifi_transport_rx(buf, sizeof buf, &rssi, &channel, CONFIG_STARK_WIFI_POLL_MS);
        if (n > 0u) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            (void)wifi_svc_on_frame(&s_svc, buf, n, rssi, channel);
            xSemaphoreGive(s_lock);
        }
    }
}

stark_err_t stark_wifi_init(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    wifi_svc_init(&s_svc);
    if (xTaskCreateStaticPinnedToCore(
            wifi_task, "stark_wifi", (uint32_t)(sizeof s_stack / sizeof s_stack[0]), NULL,
            CONFIG_STARK_WIFI_TASK_PRIO, s_stack, &s_tcb, WIFI_CORE) == NULL) {
        return STARK_ERR_NO_MEM;
    }
    return STARK_OK;
}

stark_err_t stark_wifi_start(void)
{
    if (atomic_load(&s_run)) {
        return STARK_OK;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    wifi_svc_init(&s_svc);
    xSemaphoreGive(s_lock);

    stark_err_t err = stark_wifi_transport_open();
    if (err != STARK_OK) {
        STARK_LOGW("wifi", "radio did not come up");
        atomic_store(&s_ready, false);
        publish_module();
        return err;
    }
    atomic_store(&s_ready, true);
    atomic_store(&s_run, true);
    publish_module();
    return STARK_OK;
}

void stark_wifi_stop(void)
{
    if (!atomic_load(&s_run)) {
        return;
    }
    atomic_store(&s_run, false);
    atomic_store(&s_ready, false);
    stark_wifi_transport_close();
    publish_module();
}

bool stark_wifi_ready(void)
{
    return atomic_load(&s_ready);
}

void stark_wifi_survey_copy(wifi_survey_t *out)
{
    if (out == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_svc.survey;
    xSemaphoreGive(s_lock);
}
