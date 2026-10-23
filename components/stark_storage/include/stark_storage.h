/*
 * stark_storage.h — L3 storage service (TASKS.md STARK-0201, ADR-0018).
 *
 * Mounts the card's FAT volume at STARK_STORAGE_ROOT in the background,
 * tracks its state and publishes every change as STARK_EVT_SYSTEM /
 * STARK_SYS_STORAGE (system.arg = the new stark_storage_state_t). The
 * medium itself (microSD over SPI2) is the stark_storage_medium port.
 */
#pragma once

#include <stdint.h>
#include "stark_err.h"
#include "stark_storage_medium.h"

#define STARK_STORAGE_ROOT "/sd"

/* STARK_EVT_SYSTEM system.id (ARCHITECTURE §6.3 keeps the id table). */
#define STARK_SYS_STORAGE  2u

/* Values are stable: they travel in event payloads. */
typedef enum {
    STARK_STORAGE_NOT_FITTED = 0, /* the board has no card slot (sd_cs == -1) */
    STARK_STORAGE_ABSENT,         /* no card answers */
    STARK_STORAGE_MOUNTED,        /* FAT volume at STARK_STORAGE_ROOT */
    STARK_STORAGE_UNUSABLE,       /* a card answers but holds no usable FAT volume */
    STARK_STORAGE_REMOVED,        /* the mounted card went away */
    STARK_STORAGE_EJECTED,        /* unmounted on request (STARK-0202) */
} stark_storage_state_t;

/*
 * Starts the storage task and returns at once: the first mount attempt runs
 * in that task, so a missing or slow card never delays the boot. On a board
 * without a card slot it logs `storage: not fitted` and starts nothing.
 * Needs stark_board_init() (SPI2) and stark_event_init(). STARK_ERR_STATE if
 * already started, STARK_ERR_NO_MEM if the task cannot be created.
 */
stark_err_t stark_storage_init(void);

/* Current state; any task. STARK_STORAGE_NOT_FITTED before init. */
stark_storage_state_t stark_storage_state(void);

/* Capacity of the mounted volume. STARK_ERR_INVALID_ARG for NULL,
 * STARK_ERR_STATE unless mounted. Blocks on the card: not from the UI task. */
stark_err_t stark_storage_space(stark_medium_space_t *out);

/* Stable lower-case name of a state ("no card", "mounted", ...), for logs. */
const char *stark_storage_state_str(stark_storage_state_t state);
