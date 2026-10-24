/*
 * stark_storage_medium.h — L1 port: one FAT volume at a mount point
 * (TASKS.md STARK-0201, ADR-0018 §2).
 *
 * The production implementation is microSD over the shared SPI2 bus
 * (medium_sdspi.c). Tests link a substitute with the same interface; the
 * firmware contains no other implementation and no switch between them.
 * Only stark_storage calls this, always under its own mutex, so none of
 * these functions is re-entrant.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

typedef struct {
    uint64_t total_bytes;
    uint64_t free_bytes;
} stark_medium_space_t;

/* Short stable name for logs ("sdspi"). */
const char *stark_storage_medium_name(void);

/*
 * Mounts the volume at mount_point with room for max_files open files.
 * STARK_OK; STARK_ERR_NOT_FOUND when no card answers; STARK_ERR_NOT_SUPPORTED
 * when a card answers but holds no FAT volume; STARK_ERR_IO otherwise. Never
 * formats anything (ADR-0018 §6).
 */
stark_err_t stark_storage_medium_mount(const char *mount_point, size_t max_files);

/* Unmounts and releases the card. Idempotent. */
void stark_storage_medium_unmount(void);

/* While mounted: does the card still answer? STARK_OK, STARK_ERR_NOT_FOUND,
 * or STARK_ERR_STATE when nothing is mounted. */
stark_err_t stark_storage_medium_probe(void);

/* While unmounted (ejected): does a card answer at all? Initialises the
 * card without mounting it. STARK_OK, STARK_ERR_NOT_FOUND, or STARK_ERR_STATE
 * while mounted. (STARK-0202) */
stark_err_t stark_storage_medium_present(void);

/* Capacity of the mounted volume; STARK_ERR_STATE when nothing is mounted. */
stark_err_t stark_storage_medium_space(stark_medium_space_t *out);
