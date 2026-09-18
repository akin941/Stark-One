/*
 * stark_main.c — application entry point
 */
#include "stark_log.h"
#include "stark_err.h"
#include "stark_board.h"
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
     * not be subtle" means. No stark_panic()/stark_hal exists yet
     * (STARK-0007), so a bad pin map halts here rather than rebooting.
     */
    stark_err_t err = stark_board_init();
    if (err != STARK_OK) {
        STARK_LOGE("boot", "board init failed: %s — halting", stark_err_str(err));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    STARK_LOGI("board", "%s pins ok spi2 ready", stark_board_name());
}