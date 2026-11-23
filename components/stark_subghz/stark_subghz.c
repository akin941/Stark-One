/*
 * stark_subghz.c — the Sub-GHz service's target port (stark_subghz.h): binds
 * the pure service (subghz_svc.c) to stark_module's claims, the transport and
 * FreeRTOS time, and runs the service task. Every transport call after init is
 * made by that task. Target-only.
 */
#include "stark_subghz.h"
#include <stdatomic.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
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

static void io_log(void *ctx, subghz_log_t what, unsigned a, unsigned b)
{
    (void)ctx;
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

static const subghz_svc_io_t k_io = {io_list,  io_claim, io_release, io_lease_valid,
                                     io_open,  io_close, io_xfer,    io_now,
                                     io_delay, io_log,   io_changed, NULL};

static void subghz_task(void *arg)
{
    (void)arg;
    for (;;) {
        subghz_svc_step(&s_svc);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_STARK_SUBGHZ_POLL_MS));
    }
}

stark_err_t stark_subghz_init(void)
{
    subghz_svc_init(&s_svc, &k_io);
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
