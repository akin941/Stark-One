/*
 * settings_apply.c — settings -> services (settings_apply.h). Change events
 * arrive on the UI task (the bus's one consumer).
 */
#include "settings_apply.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "stark_buzzer.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_input.h"
#include "stark_log.h"
#include "stark_settings.h"

/* Key repeat presets: Slow, Normal (the Kconfig timing), Fast. All inside
 * the Kconfig ranges (ARCHITECTURE §6.6). */
static const uint16_t k_repeat[3][2] = {
    {600, 180},
    {CONFIG_STARK_INPUT_REPEAT_DELAY_MS, CONFIG_STARK_INPUT_REPEAT_INTERVAL_MS},
    {250, 60},
};

static void apply_log_level(uint8_t v)
{
    static const esp_log_level_t k_level[4] = {ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO,
                                               ESP_LOG_DEBUG};
    esp_log_level_t level = k_level[v];
    stark_log_set_level("*", (int)level);
    /* `key:` lines are DEBUG by contract (TESTING.md §4): kept at Info and
     * Debug, quietened with everything else at Warn and Error. */
    stark_log_set_level("key", (int)(level >= ESP_LOG_INFO ? ESP_LOG_DEBUG : level));
}

static void apply(stark_setting_t key)
{
    uint8_t v = stark_settings_get(key);
    stark_err_t err = STARK_OK;
    switch (key) {
        case STARK_SET_BRIGHTNESS:
            err = stark_display_set_backlight(v);
            break;
        case STARK_SET_VOLUME:
            err = stark_buzzer_set_volume(v);
            break;
        case STARK_SET_REPEAT:
            err = stark_input_set_timing(k_repeat[v][0], k_repeat[v][1]);
            break;
        case STARK_SET_LOG_LEVEL:
            apply_log_level(v);
            break;
        case STARK_SET_COUNT:
            return;
    }
    if (err != STARK_OK) {
        STARK_LOGW("settings", "apply %s failed: %s", stark_settings_desc(key)->name,
                   stark_err_str(err));
        return;
    }
    STARK_LOGI("settings", "applied %s=%u", stark_settings_desc(key)->name, (unsigned)v);
}

static void on_setting(const stark_event_t *e, void *ctx)
{
    (void)ctx;
    if (e->system.id == STARK_SYS_SETTING && e->system.arg >= 0 &&
        e->system.arg < (int32_t)STARK_SET_COUNT) {
        apply((stark_setting_t)e->system.arg);
    }
}

stark_err_t settings_apply_init(void)
{
    for (int k = 0; k < STARK_SET_COUNT; k++) {
        apply((stark_setting_t)k);
    }
    return stark_event_subscribe(STARK_EVT_MASK(STARK_EVT_SYSTEM), on_setting, NULL);
}
