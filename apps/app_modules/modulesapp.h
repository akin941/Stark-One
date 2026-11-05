/*
 * modulesapp.h — private to app_modules: its text, built fresh from
 * stark_module's table so nothing stale is ever shown.
 */
#pragma once

#include <stddef.h>
#include "ui_menu.h"

#define MODULES_DETAIL_ROWS 12

/* One row per entry: IR, the NFC reader, then the slots. */
const ui_menu_item_t *modulesview_list(size_t *count);

/* The entry at list row `index`, read-only. */
const ui_menu_item_t *modulesview_detail(size_t index, size_t *count);

/* The right-hand text of the row last built. */
const char *modulesview_value(size_t index);
