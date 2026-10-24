/*
 * settings_core.c — the settings table and logic (settings_core.h). Pure.
 */
#include "settings_core.h"
#include <stddef.h>

static const char *const k_volume[] = {"Off", "Low", "Medium", "High"};
static const char *const k_repeat[] = {"Slow", "Normal", "Fast"};
static const char *const k_loglvl[] = {"Error", "Warn", "Info", "Debug"};

/* The only place a setting is defined (TASKS.md STARK-0204). */
static const stark_setting_desc_t k_desc[STARK_SET_COUNT] = {
    [STARK_SET_BRIGHTNESS] = {"brightness", "bright", 10, 100, 10, 100, NULL},
    [STARK_SET_VOLUME] = {"volume", "volume", 0, 3, 1, 3, k_volume},
    [STARK_SET_REPEAT] = {"repeat", "repeat", 0, 2, 1, 1, k_repeat},
    [STARK_SET_LOG_LEVEL] = {"loglvl", "loglvl", 0, 3, 1, 2, k_loglvl},
};

const stark_setting_desc_t *settings_desc(stark_setting_t key)
{
    return (unsigned)key < STARK_SET_COUNT ? &k_desc[key] : NULL;
}

bool settings_valid(stark_setting_t key, uint8_t value)
{
    const stark_setting_desc_t *d = settings_desc(key);
    return d != NULL && value >= d->min && value <= d->max && (value - d->min) % d->step == 0;
}

static void notify(const settings_hooks_t *h, stark_setting_t key, uint8_t value)
{
    if (h != NULL && h->changed != NULL) {
        h->changed(key, value, h->ctx);
    }
}

void settings_core_load(settings_core_t *c, const settings_store_t *store,
                        const settings_hooks_t *hooks)
{
    for (int k = 0; k < STARK_SET_COUNT; k++) {
        c->value[k] = k_desc[k].def;
        c->dirty[k] = false;
    }
    c->store = store;
    c->schema_dirty = false;
    c->source = SETTINGS_FROM_RAM;
    if (store == NULL) {
        return;
    }
    uint8_t schema = 0;
    stark_err_t err = store->get_u8(store->ctx, "schema", &schema);
    if (err == STARK_ERR_NOT_FOUND) {
        c->source = SETTINGS_FROM_DEFAULTS; /* a new store */
        c->schema_dirty = true;
        uint32_t n = 0;
        (void)settings_core_commit(c, &n); /* a failure here is retried at the next commit */
        return;
    }
    if (err != STARK_OK) {
        c->store = NULL; /* unreadable: RAM only */
        return;
    }
    c->source = SETTINGS_FROM_NVS;
    for (int k = 0; k < STARK_SET_COUNT; k++) {
        uint8_t v = 0;
        err = store->get_u8(store->ctx, k_desc[k].key, &v);
        if (err == STARK_OK && settings_valid((stark_setting_t)k, v)) {
            c->value[k] = v;
        } else if (err != STARK_ERR_NOT_FOUND) { /* out of range, or unreadable */
            if (hooks != NULL && hooks->invalid != NULL) {
                hooks->invalid((stark_setting_t)k, v, hooks->ctx);
            }
            c->dirty[k] = true; /* rewritten with the default at the next commit */
        }
    }
}

stark_err_t settings_core_set(settings_core_t *c, stark_setting_t key, uint8_t value,
                              const settings_hooks_t *hooks)
{
    if (!settings_valid(key, value)) {
        return STARK_ERR_INVALID_ARG;
    }
    if (c->value[key] != value) {
        c->value[key] = value;
        c->dirty[key] = true;
        notify(hooks, key, value);
    }
    return STARK_OK;
}

stark_err_t settings_core_commit(settings_core_t *c, uint32_t *written)
{
    *written = 0;
    if (c->store == NULL) {
        return STARK_ERR_IO;
    }
    const settings_store_t *s = c->store;
    uint32_t n = 0;
    if (c->schema_dirty) {
        STARK_CHECK_RET(s->set_u8(s->ctx, "schema", (uint8_t)SETTINGS_SCHEMA));
    }
    for (int k = 0; k < STARK_SET_COUNT; k++) {
        if (c->dirty[k]) {
            STARK_CHECK_RET(s->set_u8(s->ctx, k_desc[k].key, c->value[k]));
            n++;
        }
    }
    if (n == 0 && !c->schema_dirty) {
        return STARK_OK; /* nothing to write */
    }
    STARK_CHECK_RET(s->commit(s->ctx));
    c->schema_dirty = false;
    for (int k = 0; k < STARK_SET_COUNT; k++) {
        c->dirty[k] = false;
    }
    *written = n;
    return STARK_OK;
}

void settings_core_restore(settings_core_t *c, const settings_hooks_t *hooks)
{
    for (int k = 0; k < STARK_SET_COUNT; k++) {
        (void)settings_core_set(c, (stark_setting_t)k, k_desc[k].def, hooks);
    }
}
