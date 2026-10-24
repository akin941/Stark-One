/*
 * settings_apply.h — the composition root applies settings to the services
 * that own them (TASKS.md STARK-0204): L3 services may not call each other,
 * so stark_settings only publishes STARK_SYS_SETTING and main does the rest.
 */
#pragma once

#include "stark_err.h"

/* Applies every setting once (boot) and subscribes to later changes.
 * Call after the display, buzzer and input services have started. */
stark_err_t settings_apply_init(void);
