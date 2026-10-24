/*
 * stark_storage.c — the storage task (stark_storage.h). Target-only; every
 * decision is storage_core.c's (host-tested), this file only performs the
 * actions it returns and reports the state changes.
 *
 * The medium is called only from here, under s_lock (ADR-0018 §4): the card
 * is a device on the shared SPI2 bus, and the ESP-IDF bus lock interleaves
 * its transactions with the display's.
 */
#include "stark_storage.h"
#include <stdatomic.h>
#include <stdbool.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "stark_board.h"
#include "stark_event.h"
#include "stark_log.h"
#include "stark_storage_medium.h"
#include "storage_core.h"
#include "storage_file.h"
#include "storage_log.h"

#define STORAGE_CORE 0 /* the UI task runs on core 1 (ARCHITECTURE §8) */

static StaticTask_t s_tcb;
static StackType_t s_stack[CONFIG_STARK_STORAGE_TASK_STACK / sizeof(StackType_t)];
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static storage_core_t s_core; /* guarded by s_lock */
static atomic_int s_state = STARK_STORAGE_NOT_FITTED;
static atomic_bool s_req_eject;
static atomic_bool s_req_mount;
static atomic_bool s_io_error;
static uint32_t s_next_flush_ms; /* storage task only */
static portMUX_TYPE s_space_lock = portMUX_INITIALIZER_UNLOCKED;
static stark_medium_space_t s_space; /* cached at mount, under s_space_lock */

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void publish(stark_storage_state_t state)
{
    atomic_store(&s_state, (int)state);
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_STORAGE;
    e.system.arg = (int32_t)state;
    (void)stark_event_publish(&e);
}

/* One line per state change (TASKS.md STARK-0201 log contract). */
static void report(stark_storage_state_t state, stark_err_t last)
{
    stark_medium_space_t space = {0};
    switch (state) {
        case STARK_STORAGE_MOUNTED:
            (void)stark_storage_medium_space(&space);
            portENTER_CRITICAL(&s_space_lock);
            s_space = space;
            portEXIT_CRITICAL(&s_space_lock);
            STARK_LOGI("storage", "mounted %s total=%u free=%u", stark_storage_medium_name(),
                       (unsigned)(space.total_bytes / 1024u), (unsigned)(space.free_bytes / 1024u));
            storage_log_on_mount();
            uint32_t swept = 0;
            stark_err_t prep = storage_files_prepare(&swept);
            if (prep != STARK_OK) {
                STARK_LOGW("storage", "cannot prepare directories: %s", stark_err_str(prep));
            } else if (swept > 0) {
                STARK_LOGI("storage", "swept %u tmp", (unsigned)swept);
            }
            break;
        case STARK_STORAGE_ABSENT:
            STARK_LOGI("storage", "no card");
            break;
        case STARK_STORAGE_UNUSABLE:
            STARK_LOGW("storage", "unusable card: %s", stark_err_str(last));
            break;
        case STARK_STORAGE_REMOVED:
            STARK_LOGW("storage", "card removed");
            break;
        case STARK_STORAGE_EJECTED:
            STARK_LOGI("storage", "ejected log=%u", (unsigned)storage_log_written());
            break;
        case STARK_STORAGE_NOT_FITTED:
            STARK_LOGI("storage", "%s", stark_storage_state_str(state));
            break;
    }
    publish(state);
}

/* ---- the file helpers' environment (storage_file.h) ------------------ */

static void env_lock(void *ctx)
{
    (void)ctx;
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void env_unlock(void *ctx)
{
    (void)ctx;
    (void)xSemaphoreGive(s_lock);
}

static bool env_mounted(void *ctx, uint32_t *generation)
{
    (void)ctx;
    *generation = s_core.generation;
    return s_core.state == STARK_STORAGE_MOUNTED;
}

/* Called by a helper (any task) under the lock: probe at once. */
static void env_io_error(void *ctx)
{
    (void)ctx;
    atomic_store(&s_io_error, true);
    xTaskNotifyGive(s_task);
}

/* Runs one input through the core and every action that follows from it. */
static void run(storage_input_t in, stark_err_t result)
{
    storage_step_t step = storage_core_step(&s_core, in, result, now_ms());
    for (;;) {
        if (step.changed) {
            report(s_core.state, result);
        }
        switch (step.action) {
            case STORAGE_ACT_TRY_MOUNT:
                result = stark_storage_medium_mount(STARK_STORAGE_ROOT, STORAGE_FILE_SLOTS);
                step = storage_core_step(&s_core, STORAGE_IN_MOUNT_RESULT, result, now_ms());
                continue;
            case STORAGE_ACT_PROBE:
                result = stark_storage_medium_probe();
                step = storage_core_step(&s_core, STORAGE_IN_PROBE_RESULT, result, now_ms());
                continue;
            case STORAGE_ACT_PRESENT_CHECK:
                result = stark_storage_medium_present();
                step = storage_core_step(&s_core, STORAGE_IN_PRESENT_RESULT, result, now_ms());
                continue;
            case STORAGE_ACT_UNMOUNT:
                storage_files_invalidate(); /* every open handle goes stale first */
                stark_storage_medium_unmount();
                break;
            case STORAGE_ACT_NONE:
                break;
        }
        return;
    }
}

/* Outside the lock (the helpers take it per call): drain the capture ring
 * into the log file. The writer logs nothing itself; its first write after a
 * mount is announced here, once. */
static void flush_log(void)
{
    bool first = false;
    if (stark_storage_state() == STARK_STORAGE_MOUNTED && storage_log_flush(&first) == STARK_OK &&
        first) {
        STARK_LOGI("storage", "logging to logs/stark.log");
    }
}

static void storage_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* Due, or an eject is waiting: the log goes out first, so an eject
         * never loses buffered lines and the writer holds no file. */
        if (atomic_load(&s_req_eject) || (int32_t)(now_ms() - s_next_flush_ms) >= 0) {
            flush_log();
            s_next_flush_ms = now_ms() + CONFIG_STARK_STORAGE_LOG_FLUSH_MS;
        }
        (void)xSemaphoreTake(s_lock, portMAX_DELAY);
        if (atomic_exchange(&s_req_eject, false)) {
            uint32_t open = storage_files_open_count();
            if (open > 0 && s_core.state == STARK_STORAGE_MOUNTED) {
                STARK_LOGW("storage", "eject refused: %u open", (unsigned)open);
                publish(s_core.state); /* unchanged: lets the UI say so */
            } else {
                run(STORAGE_IN_REQ_EJECT, STARK_OK);
            }
        }
        if (atomic_exchange(&s_req_mount, false)) {
            run(STORAGE_IN_REQ_MOUNT, STARK_OK);
        }
        if (atomic_exchange(&s_io_error, false)) {
            run(STORAGE_IN_IO_ERROR, STARK_OK);
        }
        run(STORAGE_IN_TICK, STARK_OK);
        uint32_t wait = storage_core_wait_ms(&s_core, now_ms());
        uint32_t to_flush = s_next_flush_ms - now_ms();
        if (s_core.state == STARK_STORAGE_MOUNTED && (int32_t)to_flush >= 0 && to_flush < wait) {
            wait = to_flush;
        }
        (void)xSemaphoreGive(s_lock);
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));
    }
}

stark_err_t stark_storage_init(void)
{
    if (s_lock != NULL) {
        return STARK_ERR_STATE;
    }
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    bool fitted = stark_board_pins()->sd_cs >= 0;
    storage_timing_t timing = {CONFIG_STARK_STORAGE_RETRY_MS, CONFIG_STARK_STORAGE_PROBE_MS};
    storage_core_init(&s_core, fitted, timing, now_ms());
    const storage_env_t env = {
        .io = &storage_io_posix,
        .root = STARK_STORAGE_ROOT,
        .lock = env_lock,
        .unlock = env_unlock,
        .mounted = env_mounted,
        .io_error = env_io_error,
    };
    storage_files_init(&env);
    storage_log_init(CONFIG_STARK_LOG_RING_BYTES > 0 ? stark_log_ring_take : NULL,
                     (uint32_t)CONFIG_STARK_STORAGE_LOG_FILE_KB * 1024u,
                     CONFIG_STARK_STORAGE_LOG_FILES);
    if (!fitted) {
        report(STARK_STORAGE_NOT_FITTED, STARK_OK);
        return STARK_OK;
    }
    s_task = xTaskCreateStaticPinnedToCore(
        storage_task, "stark_storage", (uint32_t)(sizeof s_stack / sizeof s_stack[0]), NULL,
        CONFIG_STARK_STORAGE_TASK_PRIO, s_stack, &s_tcb, STORAGE_CORE);
    return s_task != NULL ? STARK_OK : STARK_ERR_NO_MEM;
}

static stark_err_t request(atomic_bool *flag)
{
    if (s_task == NULL) {
        return STARK_ERR_STATE;
    }
    atomic_store(flag, true);
    xTaskNotifyGive(s_task);
    return STARK_OK;
}

stark_err_t stark_storage_request_eject(void)
{
    return request(&s_req_eject);
}

stark_err_t stark_storage_request_mount(void)
{
    return request(&s_req_mount);
}

stark_storage_state_t stark_storage_state(void)
{
    return (stark_storage_state_t)atomic_load(&s_state);
}

stark_err_t stark_storage_space(stark_medium_space_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (stark_storage_state() != STARK_STORAGE_MOUNTED) {
        return STARK_ERR_STATE;
    }
    portENTER_CRITICAL(&s_space_lock);
    *out = s_space;
    portEXIT_CRITICAL(&s_space_lock);
    return STARK_OK;
}
