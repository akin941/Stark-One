/*
 * ui_port_settings.c — host UI test port: stark_settings.h over the real
 * settings_core.c and the fake NVS (test/host/support/fake_nvs.c), so the
 * real Settings app runs on the host (TASKS.md STARK-0205). Changes are
 * published on the bus as the NVS port does; logging matches its lines.
 */
#include "fake_nvs.h"
#include "settings_core.h"
#include "stark_event.h"
#include "stark_log.h"
#include "stark_settings.h"

static settings_core_t s_core;
static bool s_ready;

static void changed(stark_setting_t key, uint8_t value, void *ctx)
{
    (void)ctx;
    STARK_LOGI("settings", "%s=%u", settings_desc(key)->name, (unsigned)value);
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_SETTING;
    e.system.arg = (int32_t)key;
    (void)stark_event_publish(&e);
}

static const settings_hooks_t k_hooks = {changed, NULL, NULL};

stark_err_t stark_settings_init(void)
{
    settings_core_load(&s_core, &fake_nvs, &k_hooks);
    s_ready = true;
    return STARK_OK;
}

uint8_t stark_settings_get(stark_setting_t key)
{
    return s_ready ? s_core.value[key] : settings_desc(key)->def;
}

stark_err_t stark_settings_set(stark_setting_t key, uint8_t value)
{
    return settings_core_set(&s_core, key, value, &k_hooks);
}

stark_err_t stark_settings_commit(void)
{
    uint32_t n = 0;
    stark_err_t err = settings_core_commit(&s_core, &n);
    if (err == STARK_OK && n > 0) {
        STARK_LOGI("settings", "committed n=%u", (unsigned)n);
    }
    return err;
}

stark_err_t stark_settings_restore_defaults(void)
{
    settings_core_restore(&s_core, &k_hooks);
    return stark_settings_commit();
}

const stark_setting_desc_t *stark_settings_desc(stark_setting_t key)
{
    return settings_desc(key);
}
