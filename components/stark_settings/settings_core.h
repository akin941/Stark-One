/*
 * settings_core.h — private to stark_settings: the settings table,
 * validation and the load/set/commit logic over an injected store
 * (TASKS.md STARK-0204). Pure; host-tested at 100 % of lines against a fake
 * NVS with failure switches.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stark_err.h"
#include "stark_settings.h"

#define SETTINGS_SCHEMA 1u

typedef struct {
    /* STARK_OK, STARK_ERR_NOT_FOUND (no such key) or another error. */
    stark_err_t (*get_u8)(void *ctx, const char *key, uint8_t *out);
    stark_err_t (*set_u8)(void *ctx, const char *key, uint8_t value);
    stark_err_t (*commit)(void *ctx);
    void *ctx;
} settings_store_t;

typedef enum { SETTINGS_FROM_NVS, SETTINGS_FROM_DEFAULTS, SETTINGS_FROM_RAM } settings_source_t;

typedef struct {
    uint8_t value[STARK_SET_COUNT];
    bool dirty[STARK_SET_COUNT];
    bool schema_dirty;
    const settings_store_t *store; /* NULL: RAM only */
    settings_source_t source;
} settings_core_t;

/* Logging and change notification, injected so the core stays pure. */
typedef struct {
    void (*changed)(stark_setting_t key, uint8_t value, void *ctx);
    void (*invalid)(stark_setting_t key, uint8_t value, void *ctx); /* stored value rejected */
    void *ctx;
} settings_hooks_t;

const stark_setting_desc_t *settings_desc(stark_setting_t key);
bool settings_valid(stark_setting_t key, uint8_t value);

/* Loads from store (NULL: defaults in RAM only, source RAM). Missing keys
 * take their default; invalid ones too (reported, and rewritten at the next
 * commit). A store with no schema key is new: source DEFAULTS, and the
 * schema is written at once. A read error other than NOT_FOUND drops to
 * RAM only. */
void settings_core_load(settings_core_t *c, const settings_store_t *store,
                        const settings_hooks_t *hooks);

stark_err_t settings_core_set(settings_core_t *c, stark_setting_t key, uint8_t value,
                              const settings_hooks_t *hooks);

/* Writes dirty keys and commits; *written = how many keys. RAM only:
 * STARK_ERR_IO. On a failure nothing is marked clean. */
stark_err_t settings_core_commit(settings_core_t *c, uint32_t *written);

void settings_core_restore(settings_core_t *c, const settings_hooks_t *hooks);
