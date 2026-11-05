/*
 * stark_module.c — the module service's target port (stark_module.h): binds
 * the pure core to stark_module_transport and runs the polling task. It cannot
 * live on esp_timer: a bus call may block for its timeout. Target-only.
 */
#include "stark_module.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "module_core.h"
#include "sdkconfig.h"
#include "stark_event.h"
#include "stark_log.h"
#include "stark_module_transport.h"

#define MODULE_CORE 0 /* the UI task runs on core 1 */

static const uint8_t k_addrs[STARK_MODULE_SLOTS] = {0x50, 0x51}; /* HARDWARE.md §6 */

static module_core_t s_core;
static StackType_t s_stack[CONFIG_STARK_MODULE_TASK_STACK / sizeof(StackType_t)];
static StaticTask_t s_tcb;
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;
static uint32_t s_logged_caps;

static stark_err_t io_probe(void *ctx, uint8_t addr)
{
    (void)ctx;
    return stark_module_transport_probe(addr, CONFIG_STARK_MODULE_I2C_TIMEOUT_MS);
}

static stark_err_t io_read(void *ctx, uint8_t addr, uint8_t off, uint8_t *buf, size_t len)
{
    (void)ctx;
    return stark_module_transport_read(addr, off, buf, len, CONFIG_STARK_MODULE_I2C_TIMEOUT_MS);
}

static void io_lock(void *ctx)
{
    (void)ctx;
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void io_unlock(void *ctx)
{
    (void)ctx;
    (void)xSemaphoreGive(s_lock);
}

static const module_io_t k_io = {io_probe, io_read, io_lock, io_unlock, NULL};

static void log_slot(const stark_module_entry_t *e)
{
    switch (e->state) {
        case STARK_MODULE_ABSENT:
            STARK_LOGI("module", "slot %u no id", (unsigned)e->slot);
            break;
        case STARK_MODULE_KNOWN:
            STARK_LOGI("module", "slot %u %s id=0x%04x rev=%u caps=0x%08x", (unsigned)e->slot,
                       e->name, (unsigned)e->id, (unsigned)e->rev, (unsigned)e->declared);
            break;
        case STARK_MODULE_GENERIC:
            STARK_LOGW("module", "slot %u generic (%s)", (unsigned)e->slot,
                       e->unsupported ? "unsupported caps" : module_desc_result_str(e->why));
            break;
        case STARK_MODULE_CONFLICT:
            STARK_LOGW("module", "slot %u conflict %s", (unsigned)e->slot, e->name);
            break;
    }
}

static void publish_change(void)
{
    uint32_t caps = module_core_caps(&s_core);
    if (caps != s_logged_caps) {
        s_logged_caps = caps;
        STARK_LOGI("module", "caps 0x%08x", (unsigned)caps);
    }
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_MODULE;
    (void)stark_event_publish(&e);
}

static void log_all(void)
{
    stark_module_entry_t list[STARK_MODULE_SLOTS];
    size_t n = module_core_list(&s_core, list, STARK_MODULE_SLOTS);
    for (size_t i = 0; i < n; i++) {
        log_slot(&list[i]);
    }
}

static void module_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_STARK_MODULE_POLL_MS));
        uint32_t changed = module_core_tick(&s_core);
        if (changed != 0) {
            stark_module_entry_t list[STARK_MODULE_SLOTS];
            size_t n = module_core_list(&s_core, list, STARK_MODULE_SLOTS);
            for (size_t i = 0; i < n; i++) {
                if ((changed & (UINT32_C(1) << i)) != 0) {
                    log_slot(&list[i]);
                }
            }
            publish_change();
        }
    }
}

stark_err_t stark_module_init(void)
{
    if (s_lock != NULL) {
        return STARK_ERR_STATE;
    }
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    module_core_init(&s_core, &k_io, k_addrs, STARK_MODULE_SLOTS);
    stark_err_t err = stark_module_transport_open();
    if (err != STARK_OK) {
        STARK_LOGI("module", "not fitted (%s)", stark_err_str(err));
        log_all();
        return STARK_OK;
    }
    /* Two rounds: a descriptor is accepted after two identical reads. */
    (void)module_core_tick(&s_core);
    (void)module_core_tick(&s_core);
    log_all();
    if (xTaskCreateStaticPinnedToCore(
            module_task, "stark_module", (uint32_t)(sizeof s_stack / sizeof s_stack[0]), NULL,
            CONFIG_STARK_MODULE_TASK_PRIO, s_stack, &s_tcb, MODULE_CORE) == NULL) {
        return STARK_ERR_NO_MEM;
    }
    return STARK_OK;
}

size_t stark_module_list(stark_module_entry_t *out, size_t max)
{
    return s_lock != NULL ? module_core_list(&s_core, out, max) : 0;
}

uint32_t stark_module_caps(void)
{
    return s_lock != NULL ? module_core_caps(&s_core) : 0;
}

void stark_module_confirm(uint32_t caps, bool present)
{
    if (s_lock != NULL && module_core_confirm(&s_core, caps, present)) {
        publish_change();
    }
}
