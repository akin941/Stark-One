/*
 * fake_nvs.c — fake_nvs.h: pending writes become visible on commit, as NVS
 * guarantees only after nvs_commit().
 */
#include "fake_nvs.h"
#include <string.h>

#define SLOTS 16

typedef struct {
    char key[16];
    uint8_t value;
    bool used;
} entry_t;

static entry_t s_committed[SLOTS], s_pending[SLOTS];
static int s_writes;
static char s_get_key[16];
static bool s_get_all;
static stark_err_t s_get_err, s_set_err, s_commit_err;

static entry_t *find(entry_t *t, const char *key, bool create)
{
    for (int i = 0; i < SLOTS; i++) {
        if (t[i].used && strcmp(t[i].key, key) == 0) {
            return &t[i];
        }
    }
    for (int i = 0; create && i < SLOTS; i++) {
        if (!t[i].used) {
            t[i].used = true;
            strncpy(t[i].key, key, sizeof t[i].key - 1);
            return &t[i];
        }
    }
    return NULL;
}

void fake_nvs_reset(void)
{
    memset(s_committed, 0, sizeof s_committed);
    memset(s_pending, 0, sizeof s_pending);
    s_writes = 0;
    s_get_err = s_set_err = s_commit_err = STARK_OK;
    s_get_all = false;
    s_get_key[0] = '\0';
}

void fake_nvs_put(const char *key, uint8_t v)
{
    find(s_committed, key, true)->value = v;
}

bool fake_nvs_value(const char *key, uint8_t *v)
{
    entry_t *e = find(s_committed, key, false);
    if (e != NULL) {
        *v = e->value;
    }
    return e != NULL;
}

int fake_nvs_writes(void)
{
    return s_writes;
}

void fake_nvs_fail_get(const char *key, stark_err_t err)
{
    s_get_err = err;
    s_get_all = key == NULL;
    strncpy(s_get_key, key != NULL ? key : "", sizeof s_get_key - 1);
}

void fake_nvs_fail_set(stark_err_t err)
{
    s_set_err = err;
}

void fake_nvs_fail_commit(stark_err_t err)
{
    s_commit_err = err;
}

static stark_err_t get(void *ctx, const char *key, uint8_t *out)
{
    (void)ctx;
    if (s_get_err != STARK_OK && (s_get_all || strcmp(key, s_get_key) == 0)) {
        return s_get_err;
    }
    entry_t *e = find(s_committed, key, false);
    if (e == NULL) {
        return STARK_ERR_NOT_FOUND;
    }
    *out = e->value;
    return STARK_OK;
}

static stark_err_t set(void *ctx, const char *key, uint8_t value)
{
    (void)ctx;
    s_writes++;
    if (s_set_err != STARK_OK) {
        return s_set_err;
    }
    find(s_pending, key, true)->value = value;
    return STARK_OK;
}

static stark_err_t commit(void *ctx)
{
    (void)ctx;
    if (s_commit_err != STARK_OK) {
        return s_commit_err;
    }
    for (int i = 0; i < SLOTS; i++) {
        if (s_pending[i].used) {
            find(s_committed, s_pending[i].key, true)->value = s_pending[i].value;
        }
    }
    memset(s_pending, 0, sizeof s_pending);
    return STARK_OK;
}

const settings_store_t fake_nvs = {get, set, commit, NULL};
