/*
 * stark_settings.c — the NVS port of stark_settings.h. Target-only; the
 * table, validation and load/commit logic are settings_core.c
 * (host-tested). set/commit/restore run on the UI task; get from anywhere
 * (one byte per setting, read atomically).
 */
#include "stark_settings.h"
#include <stdbool.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "settings_core.h"
#include "stark_event.h"
#include "stark_log.h"

static settings_core_t s_core;
static bool s_ready;
static nvs_handle_t s_nvs;

static stark_err_t from_nvs(esp_err_t e)
{
    if (e == ESP_OK) {
        return STARK_OK;
    }
    return e == ESP_ERR_NVS_NOT_FOUND ? STARK_ERR_NOT_FOUND : STARK_ERR_IO;
}

static stark_err_t nvs_get(void *ctx, const char *key, uint8_t *out)
{
    (void)ctx;
    return from_nvs(nvs_get_u8(s_nvs, key, out));
}

static stark_err_t nvs_set(void *ctx, const char *key, uint8_t value)
{
    (void)ctx;
    return from_nvs(nvs_set_u8(s_nvs, key, value));
}

static stark_err_t nvs_commit_all(void *ctx)
{
    (void)ctx;
    return from_nvs(nvs_commit(s_nvs));
}

static const settings_store_t k_store = {nvs_get, nvs_set, nvs_commit_all, NULL};

static void on_changed(stark_setting_t key, uint8_t value, void *ctx)
{
    (void)ctx;
    STARK_LOGI("settings", "%s=%u", settings_desc(key)->name, (unsigned)value);
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_SETTING;
    e.system.arg = (int32_t)key;
    (void)stark_event_publish(&e);
}

static void on_invalid(stark_setting_t key, uint8_t value, void *ctx)
{
    (void)ctx;
    STARK_LOGW("settings", "%s invalid (%u), default", settings_desc(key)->name, (unsigned)value);
}

static const settings_hooks_t k_hooks = {on_changed, on_invalid, NULL};

/* nvs_flash_init() with IDF's documented recovery for a full or outdated
 * partition; false when the store cannot be used at all. */
static bool open_store(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        STARK_LOGW("settings", "store reset (%s)", esp_err_to_name(e));
        if (nvs_flash_erase() == ESP_OK) {
            e = nvs_flash_init();
        }
    }
    if (e == ESP_OK) {
        e = nvs_open("stark", NVS_READWRITE, &s_nvs);
    }
    if (e != ESP_OK) {
        STARK_LOGW("settings", "store unavailable: %s", esp_err_to_name(e));
        return false;
    }
    return true;
}

stark_err_t stark_settings_init(void)
{
    if (s_ready) {
        return STARK_ERR_STATE;
    }
    settings_core_load(&s_core, open_store() ? &k_store : NULL, &k_hooks);
    s_ready = true;
    static const char *const k_source[] = {"nvs", "defaults", "ram"};
    STARK_LOGI("settings", "loaded brightness=%u volume=%u repeat=%u loglvl=%u (%s)",
               s_core.value[STARK_SET_BRIGHTNESS], s_core.value[STARK_SET_VOLUME],
               s_core.value[STARK_SET_REPEAT], s_core.value[STARK_SET_LOG_LEVEL],
               k_source[s_core.source]);
    return STARK_OK;
}

uint8_t stark_settings_get(stark_setting_t key)
{
    const stark_setting_desc_t *d = settings_desc(key);
    if (d == NULL) {
        return 0;
    }
    return s_ready ? s_core.value[key] : d->def;
}

stark_err_t stark_settings_set(stark_setting_t key, uint8_t value)
{
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    return settings_core_set(&s_core, key, value, &k_hooks);
}

stark_err_t stark_settings_commit(void)
{
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    uint32_t n = 0;
    stark_err_t err = settings_core_commit(&s_core, &n);
    if (err != STARK_OK) {
        STARK_LOGW("settings", "commit failed: %s", stark_err_str(err));
    } else if (n > 0) {
        STARK_LOGI("settings", "committed n=%u", (unsigned)n);
    }
    return err;
}

stark_err_t stark_settings_restore_defaults(void)
{
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    settings_core_restore(&s_core, &k_hooks);
    return stark_settings_commit();
}

const stark_setting_desc_t *stark_settings_desc(stark_setting_t key)
{
    return settings_desc(key);
}
