/*
 * stark_diag.c — the diagnostics sampler (stark_diag.h). Target-only; the
 * arithmetic and formatting are diag_core.c (host-tested).
 */
#include "stark_diag.h"
#include <stdbool.h>
#include <string.h>
#include "diag_core.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "stark_event.h"
#include "stark_hal_esp.h"
#include "stark_log.h"

#define DIAG_MAX_TASKS 16
#define HEAP_CAPS      (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

static esp_timer_handle_t s_timer;
static stark_diag_ui_fn s_ui_source;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static stark_diag_snapshot_t s_snap;
static uint32_t s_prev_frames;
static uint64_t s_prev_us;
static uint32_t s_samples;
static TaskStatus_t s_status[DIAG_MAX_TASKS];

static void sample(void *arg)
{
    (void)arg;
    stark_diag_snapshot_t s = {0};
    if (s_ui_source != NULL) {
        s_ui_source(&s.ui);
    }
    uint64_t now = (uint64_t)esp_timer_get_time();
    s.fps_x10 = diag_fps_x10(s.ui.frames, s_prev_frames, now, s_prev_us);
    s_prev_frames = s.ui.frames;
    s_prev_us = now;
    s.heap_free = (uint32_t)heap_caps_get_free_size(HEAP_CAPS);
    s.heap_min = (uint32_t)heap_caps_get_minimum_free_size(HEAP_CAPS);
    s.heap_largest = (uint32_t)heap_caps_get_largest_free_block(HEAP_CAPS);
    stark_event_stats_t ev;
    if (stark_event_stats(&ev) == STARK_OK) {
        s.ev_published = ev.published;
        s.ev_dropped = ev.dropped;
        s.ev_max_depth = ev.max_depth;
    }
    s.uptime_s = (uint32_t)(now / 1000000u);

    portENTER_CRITICAL(&s_lock);
    s_snap = s;
    portEXIT_CRITICAL(&s_lock);

    if (CONFIG_STARK_DIAG_LOG_PERIOD_S > 0 && s_samples % CONFIG_STARK_DIAG_LOG_PERIOD_S == 0) {
        char line[96];
        (void)diag_format(&s, line, sizeof line);
        STARK_LOGI("diag", "%s", line);
    }
    s_samples++;

    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_DIAG_SAMPLE;
    (void)stark_event_publish(&e);
}

stark_err_t stark_diag_init(stark_diag_ui_fn ui_source)
{
    if (s_timer != NULL) {
        return STARK_ERR_STATE;
    }
    s_ui_source = ui_source;
    s_prev_us = (uint64_t)esp_timer_get_time();
    const esp_timer_create_args_t args = {
        .callback = sample,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "stark_diag",
    };
    stark_err_t err = stark_err_from_esp(esp_timer_create(&args, &s_timer));
    if (err != STARK_OK) {
        s_timer = NULL;
        return err;
    }
    sample(NULL); /* a valid snapshot from boot on, not only after the first second */
    err = stark_err_from_esp(esp_timer_start_periodic(s_timer, 1000000u));
    if (err != STARK_OK) {
        (void)esp_timer_delete(s_timer);
        s_timer = NULL;
    }
    return err;
}

stark_err_t stark_diag_snapshot(stark_diag_snapshot_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_timer == NULL) {
        return STARK_ERR_STATE;
    }
    portENTER_CRITICAL(&s_lock);
    *out = s_snap;
    portEXIT_CRITICAL(&s_lock);
    return STARK_OK;
}

size_t stark_diag_tasks(stark_diag_task_t *out, size_t max)
{
    if (out == NULL) {
        return 0;
    }
    UBaseType_t n = uxTaskGetSystemState(s_status, DIAG_MAX_TASKS, NULL);
    size_t count = 0;
    for (UBaseType_t i = 0; i < n && count < max; i++) {
        strncpy(out[count].name, s_status[i].pcTaskName, sizeof out[count].name - 1);
        out[count].name[sizeof out[count].name - 1] = '\0';
        out[count].stack_free = (uint32_t)s_status[i].usStackHighWaterMark;
        count++;
    }
    return count;
}
