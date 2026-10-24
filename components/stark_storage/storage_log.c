/*
 * storage_log.c — the log file writer (storage_log.h).
 */
#include "storage_log.h"
#include <stdio.h>
#include <string.h>
#include "log_rotate.h"
#include "stark_storage.h"
#include "storage_file.h"

#define LOG_NAME "stark.log"

static storage_log_take_fn s_take;
static uint32_t s_limit;
static unsigned s_files;
static bool s_size_known;
static uint32_t s_size;
static uint32_t s_written;
static bool s_announced;
static char s_chunk[1024];

void storage_log_init(storage_log_take_fn take, uint32_t limit_bytes, unsigned files)
{
    s_take = take;
    s_limit = limit_bytes;
    s_files = files < 1 ? 1 : (files > LOG_ROTATE_MAX_FILES ? LOG_ROTATE_MAX_FILES : files);
    storage_log_on_mount();
}

void storage_log_on_mount(void)
{
    s_size_known = false;
    s_size = 0;
    s_written = 0;
    s_announced = false;
}

uint32_t storage_log_written(void)
{
    return s_written;
}

static bool ok_or_missing(stark_err_t err)
{
    return err == STARK_OK || err == STARK_ERR_NOT_FOUND;
}

/* Rotates if appending `add` would pass the limit; false if a step failed. */
static bool rotate(uint32_t add)
{
    log_rotate_step_t steps[LOG_ROTATE_MAX_FILES];
    size_t n = log_rotate_plan(s_size, add, s_limit, s_files, steps, LOG_ROTATE_MAX_FILES);
    for (size_t i = 0; i < n; i++) {
        char from[24];
        char to[24];
        log_rotate_name(steps[i].from, from, sizeof from);
        stark_err_t err;
        if (steps[i].to < 0) {
            err = stark_storage_remove(STARK_DIR_LOGS, from);
        } else {
            log_rotate_name(steps[i].to, to, sizeof to);
            err = storage_files_rename(STARK_DIR_LOGS, from, to);
        }
        if (!ok_or_missing(err)) {
            return false; /* the next flush tries again */
        }
    }
    if (n > 0) {
        s_size = 0;
    }
    return true;
}

static stark_err_t append(const char *marker, size_t marker_len, size_t n)
{
    stark_file_t *f = NULL;
    STARK_CHECK_RET(stark_storage_open(STARK_DIR_LOGS, LOG_NAME, STARK_FILE_APPEND, &f));
    stark_err_t err = stark_storage_write(f, marker, marker_len);
    if (err == STARK_OK) {
        err = stark_storage_write(f, s_chunk, n);
    }
    stark_err_t cerr = stark_storage_close(f);
    return err != STARK_OK ? err : cerr;
}

stark_err_t storage_log_flush(bool *first)
{
    if (first != NULL) {
        *first = false;
    }
    if (s_take == NULL) {
        return STARK_OK;
    }
    for (;;) {
        uint32_t dropped = 0;
        size_t n = s_take(s_chunk, sizeof s_chunk, &dropped);
        char marker[48] = "";
        if (dropped > 0) {
            (void)snprintf(marker, sizeof marker, "--- %u log lines dropped ---\n",
                           (unsigned)dropped);
        }
        size_t marker_len = strlen(marker);
        if (n == 0 && marker_len == 0) {
            return STARK_OK;
        }
        if (!s_size_known) {
            stark_err_t err = storage_files_size(STARK_DIR_LOGS, LOG_NAME, &s_size);
            if (err == STARK_ERR_NOT_FOUND) {
                s_size = 0;
            } else if (err != STARK_OK) {
                return err;
            }
            s_size_known = true;
        }
        uint32_t add = (uint32_t)(n + marker_len);
        (void)rotate(add); /* on failure: append to the existing file */
        stark_err_t err = append(marker, marker_len, n);
        if (err != STARK_OK) {
            s_size_known = false; /* unknown after a failed write */
            return err;
        }
        s_size += add;
        s_written += add;
        if (!s_announced && first != NULL) {
            *first = true;
        }
        s_announced = true;
    }
}
