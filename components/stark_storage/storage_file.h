/*
 * storage_file.h — private to stark_storage: the file helpers' engine
 * (TASKS.md STARK-0203). It implements the public stark_storage_open/
 * read/write/close/discard/remove/list over an injected environment, so the
 * same code runs on the target (FreeRTOS mutex, POSIX over VFS/FATFS) and on
 * the host (a fake lock, POSIX over a temp directory, or a fault-injecting
 * I/O table). Host-tested.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stark_err.h"
#include "storage_io.h"

#define STORAGE_FILE_SLOTS 4 /* files open at once through the helpers */

typedef struct {
    const storage_io_t *io;
    const char *root; /* STARK_STORAGE_ROOT on the target */
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
    /* Under the lock: is a volume mounted, and which mount generation? */
    bool (*mounted)(void *ctx, uint32_t *generation);
    /* Under the lock: an operation failed with an I/O error (report once). */
    void (*io_error)(void *ctx);
    void *ctx;
} storage_env_t;

/* Installs the environment and frees every slot (nothing is closed). */
void storage_files_init(const storage_env_t *env);

/* Caller holds the lock. After a mount: creates the four directories and
 * deletes stale "*.tmp" files in them; *swept = how many. */
stark_err_t storage_files_prepare(uint32_t *swept);

/* Caller holds the lock, before the volume goes away (removal, eject):
 * closes every open descriptor and marks its handle stale — later calls on
 * it return STARK_ERR_IO, close frees the slot. */
void storage_files_invalidate(void);

/* Caller holds the lock. Handles currently open. */
uint32_t storage_files_open_count(void);
