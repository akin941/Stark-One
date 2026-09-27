/*
 * app_list.h — one extern per app (ADR-0009). Adding an app: a new
 * apps/app_<name>/ directory, one line here and one line in the
 * stark_apps[] array in app_registry.c — nothing else (ARCHITECTURE §11).
 */
#pragma once

#include "stark_app.h"
