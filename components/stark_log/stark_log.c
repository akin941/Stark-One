/*
 * stark_log.c — logging facade implementation
 */
#include "stark_log.h"
#include "esp_log.h"

void stark_log_init(void)
{
    /* esp_log is initialized by ESP-IDF before app_main.
     * This function exists as a hook for future file sink (V0.2)
     * and to make the logging dependency explicit in the component graph.
     */
}

void stark_log_set_level(const char *tag, int level)
{
    esp_log_level_set(tag, level);
}
