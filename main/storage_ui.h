/*
 * storage_ui.h — the composition root's bridge from storage to the UI
 * (TASKS.md STARK-0202). stark_storage (L3) may not call stark_ui (L4), so
 * main maps STARK_SYS_STORAGE events onto the status-bar glyph and the
 * "SD card removed" alert.
 */
#pragma once

#include "stark_err.h"

/* Subscribes to the bus. Call after stark_ui_init() and before
 * stark_storage_init(), so even the first state is shown. */
stark_err_t storage_ui_init(void);
