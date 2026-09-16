/*
 * stark_main.c — application entry point
 */
#include "stark_log.h"
#include "stark_err.h"
#include "stark_version.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_chip_info.h"
#include "esp_system.h"

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
}