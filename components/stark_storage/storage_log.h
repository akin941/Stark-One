/*
 * storage_log.h — private to stark_storage: the log file writer (TASKS.md
 * STARK-0206). Drains the capture ring into logs/stark.log through the file
 * helpers, rotating before the file would pass its size limit. It logs
 * nothing itself (a logging writer would feed itself); the storage task
 * reports what matters. Runs on the host against the POSIX ops in a temp
 * directory, with an injected ring.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

/* stark_log_ring_take() on the target; a fake on the host. */
typedef size_t (*storage_log_take_fn)(char *buf, size_t max, uint32_t *dropped);

/* take == NULL disables the writer (flush does nothing). files is clamped
 * to 1..LOG_ROTATE_MAX_FILES. */
void storage_log_init(storage_log_take_fn take, uint32_t limit_bytes, unsigned files);

/* A new volume: the file's size is read again, the byte count restarts. */
void storage_log_on_mount(void);

/*
 * Moves everything in the ring to the file. Lost lines are marked with
 * `--- <n> log lines dropped ---`. A failing rotation step leaves the
 * rotation for the next flush and appends to the existing file. On an
 * error the bytes taken are lost (at most one flush's worth). *first is set
 * true on the first successful write since the mount. Call without the
 * storage lock held (the helpers take it).
 */
stark_err_t storage_log_flush(bool *first);

/* Bytes written to the log since the mount. */
uint32_t storage_log_written(void);
