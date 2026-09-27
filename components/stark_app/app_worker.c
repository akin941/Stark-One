/*
 * app_worker.c — the running app's background worker (stark_app.h,
 * TASKS.md STARK-0108). Target-only: one static FreeRTOS task slot.
 *
 * The slot is reused only once FreeRTOS no longer owns it: the wrapper
 * signals completion and then suspends itself, and the joiner deletes the
 * task only after seeing it suspended (or, past the join timeout, deletes it
 * wherever it is and waits for the deletion to finish).
 */
#include <stdatomic.h>
#include "app_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "stark_log.h"

#define WORKER_CORE 0 /* the UI task runs on core 1 (ARCHITECTURE §8) */

static StaticTask_t s_tcb;
static StackType_t s_stack[CONFIG_STARK_APP_WORKER_STACK / sizeof(StackType_t)];
static StaticSemaphore_t s_done_buf;
static SemaphoreHandle_t s_done;
static TaskHandle_t s_task;
static atomic_bool s_stop;
static stark_app_worker_fn s_fn;
static void *s_ctx;
static uint32_t s_join_timeouts;

static void wrapper(void *arg)
{
    (void)arg;
    s_fn(s_ctx);
    (void)xSemaphoreGive(s_done);
    vTaskSuspend(NULL); /* the joiner deletes a suspended task: safe to reuse */
}

stark_err_t stark_app_worker_start(stark_app_worker_fn fn, void *ctx)
{
    if (fn == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!app_manager_running()) {
        return STARK_ERR_STATE;
    }
    if (s_task != NULL) {
        return STARK_ERR_BUSY;
    }
    if (s_done == NULL) {
        s_done = xSemaphoreCreateBinaryStatic(&s_done_buf);
    }
    (void)xSemaphoreTake(s_done, 0); /* no stale token */
    atomic_store(&s_stop, false);
    s_fn = fn;
    s_ctx = ctx;
    s_task = xTaskCreateStaticPinnedToCore(
        wrapper, "app_worker", (uint32_t)(sizeof s_stack / sizeof s_stack[0]), NULL,
        CONFIG_STARK_APP_WORKER_PRIO, s_stack, &s_tcb, WORKER_CORE);
    return s_task != NULL ? STARK_OK : STARK_ERR_NO_MEM;
}

bool stark_app_worker_should_stop(void)
{
    return atomic_load(&s_stop);
}

/* Waits (bounded) until the task is in `state` or gone. */
static void wait_state(eTaskState state)
{
    for (int i = 0; i < 100; i++) {
        eTaskState now = eTaskGetState(s_task);
        if (now == state || now == eDeleted || now == eInvalid) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void app_worker_join(const char *app_id)
{
    if (s_task == NULL) {
        return;
    }
    atomic_store(&s_stop, true);
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(CONFIG_STARK_APP_WORKER_JOIN_MS)) == pdTRUE) {
        wait_state(eSuspended); /* between the give and the self-suspend */
        vTaskDelete(s_task);
        STARK_LOGI("app", "worker %s joined", app_id);
    } else {
        s_join_timeouts++;
        STARK_LOGE("app", "worker %s join timeout", app_id);
        vTaskDelete(s_task); /* a contract violation: the worker ignored should_stop */
        wait_state(eDeleted);
    }
    s_task = NULL;
}
