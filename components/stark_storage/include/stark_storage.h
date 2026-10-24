/*
 * stark_storage.h — L3 storage service (TASKS.md STARK-0201, ADR-0018).
 *
 * Mounts the card's FAT volume at STARK_STORAGE_ROOT in the background,
 * tracks its state and publishes every change as STARK_EVT_SYSTEM /
 * STARK_SYS_STORAGE (system.arg = the new stark_storage_state_t). The
 * medium itself (microSD over SPI2) is the stark_storage_medium port.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
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

/* ---- paths and files (STARK-0203) --------------------------------------
 *
 * Files live in four directories, created at every mount:
 * STARK_STORAGE_ROOT "/stark/{captures,logs,apps,config}". A name is 1-32
 * characters of [A-Za-z0-9._-], not starting with '.' and not ending in
 * ".tmp" (reserved): anything else is STARK_ERR_INVALID_ARG, so no path can
 * leave those directories.
 *
 * These calls block on the card: call them from an app worker or the storage
 * task, never from a UI-task callback. Each one holds the storage lock for
 * its own duration only. Errors: STARK_ERR_STATE when no card is mounted,
 * STARK_ERR_BUSY when all handles are in use, STARK_ERR_NO_SPACE when the
 * card is full, STARK_ERR_NOT_FOUND for a missing file, STARK_ERR_IO for
 * anything else — including every call on a handle whose card was removed
 * or ejected since it was opened (close it to free the slot).
 */
typedef enum {
    STARK_DIR_CAPTURES,
    STARK_DIR_LOGS,
    STARK_DIR_APPS,
    STARK_DIR_CONFIG,
} stark_dir_t;

typedef enum {
    STARK_FILE_READ,         /* existing file, from the start */
    STARK_FILE_WRITE_ATOMIC, /* new content; the old file stays until close succeeds */
    STARK_FILE_APPEND,       /* created if missing */
} stark_file_mode_t;

typedef struct stark_file stark_file_t;

stark_err_t stark_storage_open(stark_dir_t dir, const char *name, stark_file_mode_t mode,
                               stark_file_t **out);
/* *got = bytes read; 0 with STARK_OK at the end of the file. */
stark_err_t stark_storage_read(stark_file_t *f, void *buf, size_t len, size_t *got);
/* Writes all len bytes or fails. */
stark_err_t stark_storage_write(stark_file_t *f, const void *buf, size_t len);
/* Flushes and frees the handle; WRITE_ATOMIC then replaces the old file
 * with the new one (FAT has no atomic rename: the old file is removed
 * first, ADR-0007). The handle is freed even when this fails. */
stark_err_t stark_storage_close(stark_file_t *f);
/* Frees the handle without committing: a WRITE_ATOMIC file is dropped. */
void stark_storage_discard(stark_file_t *f);
stark_err_t stark_storage_remove(stark_dir_t dir, const char *name);

/* Calls fn(name, size) for each file in dir (temp files hidden) until it
 * returns false. */
typedef bool (*stark_storage_list_fn)(const char *name, uint32_t size, void *ctx);
stark_err_t stark_storage_list(stark_dir_t dir, stark_storage_list_fn fn, void *ctx);
