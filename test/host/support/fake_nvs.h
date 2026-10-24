/*
 * fake_nvs.h — host test support (TASKS.md STARK-0204): an in-memory
 * settings_store_t with failure switches.
 */
#pragma once

#include "settings_core.h"

extern const settings_store_t fake_nvs;

void fake_nvs_reset(void);                        /* empty store, no failures */
void fake_nvs_put(const char *key, uint8_t v);    /* as a previous firmware wrote it */
bool fake_nvs_value(const char *key, uint8_t *v); /* committed value, if any */
int fake_nvs_writes(void);                        /* set_u8 calls since reset */
/* Failure switches (STARK_OK = no failure). get_fail applies to `key`
 * (NULL: every key). */
void fake_nvs_fail_get(const char *key, stark_err_t err);
void fake_nvs_fail_set(stark_err_t err);
void fake_nvs_fail_commit(stark_err_t err);
