/*
 * stark_main.c — application entry point
 */
#include "stark_log.h"
#include "stark_err.h"
#include "stark_board.h"
#include "stark_buzzer.h"
#include "stark_diag.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_input.h"
#include "stark_app.h"
#include "app_registry.h"
#include "stark_ui.h"
#include "stark_version.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* stark_diag is L3 and cannot read the L4 counters itself: the composition
 * root adapts them (STARK-0109). Called on the esp_timer task; both sources
 * are safe to read from any task. */
static void diag_ui_source(stark_diag_ui_t *out)
{
    stark_ui_stats_t st;
    if (stark_ui_stats(&st) == STARK_OK) {
        out->frames = st.frames;
        out->overruns = st.overruns;
        out->render_errors = st.render_errors;
    }
    out->join_timeouts = stark_app_worker_join_timeouts();
}

void app_main(void)
{
    stark_log_init();

    /* Print boot banner */
    STARK_LOGI("boot", "stark-one %s idf=%s heap=%zu", STARK_FIRMWARE_VERSION,
               esp_get_idf_version(), heap_caps_get_free_size(MALLOC_CAP_8BIT));

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    STARK_LOGI("boot", "build: %s chip: %s rev: %d", STARK_BUILD_TIMESTAMP, CONFIG_IDF_TARGET,
               chip_info.revision);

    /*
     * stark_board deliberately logs nothing itself (ARCHITECTURE.md L0/L1
     * boundary — see components/stark_board/board_devkitc1.c). main.c is
     * the composition root: it owns both the success line TASKS.md
     * STARK-0006 expects and the decision that a bad pin map is
     * boot-critical — stark_panic() logs it and reboots (ARCHITECTURE §5).
     */
    stark_err_t err = stark_board_init();
    if (err != STARK_OK) {
        stark_panic("board init", err);
    }

    STARK_LOGI("board", "%s pins ok spi2 ready", stark_board_name());

    /*
     * The heartbeat runs on the esp_timer task, independent of app_main. A
     * failure here is not fatal — the firmware keeps running, it just has no
     * visible sign of life, which is itself the diagnostic.
     */
    err = stark_board_heartbeat_start();
    if (err != STARK_OK) {
        STARK_LOGE("board", "led heartbeat failed: %s", stark_err_str(err));
    }

    err = stark_event_init();
    if (err != STARK_OK) {
        STARK_LOGE("boot", "event bus init failed: %s — no input", stark_err_str(err));
        return;
    }

    /* Boot-critical: nothing else is meaningful without the panel
     * (ARCHITECTURE §7) — a failure is a logged panic, not a black screen. */
    err = stark_display_init();
    if (err != STARK_OK) {
        stark_panic("display init", err);
    }
    /* Not boot-critical: without it the UI is merely silent. */
    err = stark_buzzer_init();
    if (err != STARK_OK) {
        STARK_LOGE("boot", "buzzer init failed: %s", stark_err_str(err));
    }

    /* `key:` lines are DEBUG by contract (TESTING.md §4) and scenarios assert
     * on them, so the one tag is enabled in every build — the same on
     * hardware and in the simulator (ADR-0010). CONFIG_LOG_MAXIMUM_LEVEL_DEBUG in
     * sdkconfig.defaults keeps DEBUG compiled in; everything else stays INFO. */
    stark_log_set_level("key", ESP_LOG_DEBUG);
    err = stark_input_start();
    if (err != STARK_OK) {
        STARK_LOGE("boot", "input start failed: %s", stark_err_str(err));
    }

    /* The UI task is the event bus's one consumer from here on (ARCHITECTURE §7). */
    err = stark_ui_init();
    if (err == STARK_OK) {
        /* The registry is the composition root's (ADR-0016). */
        err = stark_app_init(stark_apps, stark_apps_count); /* logs it, pushes the launcher */
    }
    if (err != STARK_OK) {
        stark_panic("ui init", err);
    }
    if (xTaskCreatePinnedToCore(stark_ui_task, "stark_ui", 6 * 1024, NULL, 5, NULL, 1) != pdPASS) {
        stark_panic("ui task", STARK_ERR_NO_MEM);
    }

    /* Scenarios assert that these lines appear, and in this order — never on
     * the numbers (ADR-0011; TESTING.md §1.1, §4). The heap figure is the
     * boot-time diagnostic STARK-0021 asks for; the scenario runners check
     * it against the >= 200 kB target. stark_diag (STARK-0109) then logs
     * the periodic `diag: heap=… fps=… drops=… overruns=…` line. */
    STARK_LOGI("boot", "ui_ready in %u ms", (unsigned)(esp_timer_get_time() / 1000));
    STARK_LOGI("diag", "heap=%u",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    /* Not boot-critical: without it there is simply no diagnostics. */
    err = stark_diag_init(diag_ui_source);
    if (err != STARK_OK) {
        STARK_LOGE("boot", "diag init failed: %s", stark_err_str(err));
    }
}
