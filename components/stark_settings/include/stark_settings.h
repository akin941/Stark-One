/*
 * stark_settings.h — L3 typed settings over NVS (TASKS.md STARK-0204).
 *
 * Four small settings, each a u8 with a range, a step and a default, kept
 * in RAM and persisted in the NVS namespace "stark" on commit. A change is
 * published as STARK_EVT_SYSTEM / STARK_SYS_SETTING (system.arg = the
 * stark_setting_t); the composition root applies it to the service that
 * owns it (L3 services may not call each other).
 */
#pragma once

#include <stdint.h>
#include "stark_err.h"

/* STARK_EVT_SYSTEM system.id (ARCHITECTURE §6.3 keeps the id table). */
#define STARK_SYS_SETTING 3u

typedef enum {
    STARK_SET_BRIGHTNESS, /* backlight %, 10..100 step 10, default 100 */
    STARK_SET_VOLUME,     /* 0 Off, 1 Low, 2 Medium, 3 High (default) */
    STARK_SET_REPEAT,     /* key repeat: 0 Slow, 1 Normal (default), 2 Fast */
    STARK_SET_LOG_LEVEL,  /* 0 Error, 1 Warn, 2 Info (default), 3 Debug */
    STARK_SET_COUNT
} stark_setting_t;

typedef struct {
    const char *name; /* "brightness" — logs and the Settings app */
    const char *key;  /* NVS key, <= 15 chars */
    uint8_t min, max, step, def;
    const char *const *labels; /* one per value for enumerations; NULL for numbers */
} stark_setting_desc_t;

/*
 * Opens NVS and loads every setting, falling back to defaults key by key;
 * never fails the boot. A full or outdated NVS partition is erased and
 * re-created (IDF's documented recovery: NVS holds only settings); an
 * unusable store leaves the settings in RAM only. Logs exactly one line:
 *     settings: loaded brightness=<v> volume=<v> repeat=<v> loglvl=<v> (<nvs|defaults|ram>)
 * STARK_ERR_STATE if already initialised.
 */
stark_err_t stark_settings_init(void);

/* Any task. Before init: the default. */
uint8_t stark_settings_get(stark_setting_t key);

/* UI task. Validates (STARK_ERR_INVALID_ARG out of range or off-step),
 * stores in RAM and, if the value changed, publishes STARK_SYS_SETTING and
 * logs `settings: <name>=<value>`. Nothing is written until commit. */
stark_err_t stark_settings_set(stark_setting_t key, uint8_t value);

/* Writes the changed settings to NVS: `settings: committed n=<k>` (nothing
 * changed: no write, no log). STARK_ERR_IO when the store is unavailable. */
stark_err_t stark_settings_commit(void);

/* Every setting back to its default (published as changes), then commit. */
stark_err_t stark_settings_restore_defaults(void);

/* NULL for an invalid key. */
const stark_setting_desc_t *stark_settings_desc(stark_setting_t key);
