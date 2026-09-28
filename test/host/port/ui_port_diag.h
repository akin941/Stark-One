/*
 * ui_port_diag.h — host UI test port: set what the fake stark_diag reports.
 */
#pragma once

#include <stddef.h>
#include "stark_diag.h"

void ui_port_diag_set(const stark_diag_snapshot_t *snap, const stark_diag_task_t *tasks, size_t n);
