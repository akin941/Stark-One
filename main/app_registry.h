/*
 * app_registry.h — the firmware's apps, one extern per app (ADR-0009,
 * ADR-0016). Adding an app: a new apps/app_<name>/ directory, one line here
 * and one line in stark_apps[] in app_registry.c — nothing under
 * components/ (ARCHITECTURE §11, apps/README.md).
 */
#pragma once

#include <stddef.h>
#include "stark_app.h"

extern const stark_app_t app_about;
extern const stark_app_t app_diagnostics;
extern const stark_app_t app_buzzertest;
extern const stark_app_t app_displaytest;
extern const stark_app_t app_inputtest;
extern const stark_app_t app_hello;
extern const stark_app_t app_apptest;

/* The registry, in registry order, and its length (for stark_app_init()). */
extern const stark_app_t *const stark_apps[];
extern const size_t stark_apps_count;
