/*
 * ui_port_module.h — the host UI port's fake stark_module (STARK-0505).
 */
#pragma once

#include "stark_module.h"

void ui_port_module_reset(void);
/* Replaces the table the app reads; list[] is copied. */
void ui_port_module_set(const stark_module_entry_t *list, size_t n);
