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

/*
 * Requests, safe from any task (the UI task included): both return at once
 * and the storage task acts on them; the outcome arrives as a
 * STARK_SYS_STORAGE event. Eject unmounts a mounted card (`storage: ejected`)
 * unless a file is open — then it is refused (`storage: eject refused: <n>
 * open`) and the unchanged state is published again so the UI can say so.
 * Mount tries a mount now instead of at the next retry. STARK_ERR_STATE
 * before init or without a card slot. (STARK-0202)
 */
stark_err_t stark_storage_request_eject(void);
stark_err_t stark_storage_request_mount(void);

/*
 * Requests, safe from any task (the UI task included): both return at once
 * and the storage task acts on them; the outcome arrives as a
 * STARK_SYS_STORAGE event. Eject unmounts a mounted card (`storage: ejected`)
 * unless a file is open — then it is refused (`storage: eject refused: <n>
 * open`) and the unchanged state is published again so the UI can say so.
 * An ejected card is never remounted by itself: pulling it out moves the
 * state to ABSENT, so the next insertion mounts again. Mount tries a mount
 * now instead of at the next retry. STARK_ERR_STATE before init or without
 * a card slot. (STARK-0202)
 */
stark_err_t stark_storage_request_eject(void);
stark_err_t stark_storage_request_mount(void);

/* Current state; any task. STARK_STORAGE_NOT_FITTED before init. */
stark_storage_state_t stark_storage_state(void);

/* Capacity of the mounted volume as the storage task last read it (at
 * mount): never touches the card, so any task may call it, the UI task
 * included. STARK_ERR_INVALID_ARG for NULL, STARK_ERR_STATE unless mounted. */
stark_err_t stark_storage_space(stark_medium_space_t *out);

/* Stable lower-case name of a state ("no card", "mounted", ...), for logs. */
const char *stark_storage_state_str(stark_storage_state_t state);
