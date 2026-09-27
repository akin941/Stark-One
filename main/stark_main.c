/*
 * stark_main.c — application entry point
 */
#include "stark_log.h"
#include "stark_err.h"
#include "stark_board.h"
#include "stark_event.h"
#include "stark_input.h"
#include "stark_version.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
     * STARK-0006 expects and the decision of what "a wiring mistake must
     * not be subtle" means. No stark_panic() exists yet, so a bad pin map
     * halts here rather than rebooting.
     */
    stark_err_t err = stark_board_init();
    if (err != STARK_OK) {
        STARK_LOGE("boot", "board init failed: %s — halting", stark_err_str(err));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    STARK_LOGI("board", "%s pins ok spi2 ready", stark_board_name());

    /*
     * The heartbeat runs on the esp_timer task, so app_main() may return
     * afterwards: that deletes only the main task, not the timer. A failure
     * here is not fatal — the firmware keeps running, it just has no
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

    /* `key:` lines are DEBUG by contract (TESTING.md §4) and scenarios assert
     * on them, so the one tag is enabled in every build — the same on
     * hardware and in Wokwi (ADR-0010). CONFIG_LOG_MAXIMUM_LEVEL_DEBUG in
     * sdkconfig.defaults keeps DEBUG compiled in; everything else stays INFO. */
    stark_log_set_level("key", ESP_LOG_DEBUG);
    err = stark_input_start();
    if (err != STARK_OK) {
        STARK_LOGE("boot", "input start failed: %s", stark_err_str(err));
    }

    /*
     * NOTE: interim bus consumer. The UI task (STARK-0017) becomes the one
     * consumer of the event bus; until it exists, app_main drains the bus
     * here so producers see an empty ring instead of a permanently full one
     * dropping every event (STARK-0012 AC #4). Nothing subscribes yet.
     */
    for (;;) {
        if (stark_event_wait(1000) == STARK_OK) {
            (void)stark_event_dispatch(UINT32_MAX);
        }
    }
}
