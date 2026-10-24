/*
 * ui_port_storage.h — host UI test port: a stark_storage backend whose
 * state the test sets, so the Storage app and the composition root's
 * storage bridge run on the host (TASKS.md STARK-0202).
 */
#pragma once

#include "stark_storage.h"

/* Sets the state stark_storage_state() reports and publishes it as
 * STARK_SYS_STORAGE, as the storage task would. */
void ui_port_storage_set(stark_storage_state_t state, uint64_t total, uint64_t free_bytes);
int ui_port_storage_ejects(void); /* stark_storage_request_eject() calls */
int ui_port_storage_mounts(void); /* stark_storage_request_mount() calls */
