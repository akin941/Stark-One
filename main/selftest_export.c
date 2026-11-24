/*
 * selftest_export.c — the report export (selftest_export.h). Pure over selftest_env_t; host-tested
 * at 100 % of lines, including a card that fails at every step.
 */
#include "selftest_export.h"
#include <stdint.h>
#include <string.h>
#include "selftest_report.h"

#define EXPORT_TASKS_MAX 16u
#define READBACK_CHUNK   256u
#define REPORT_DIR       STARK_DIR_LOGS

/* Everything the report needs, held on the heap while it is written (about 5.5 kB). */
typedef struct {
    stark_diag_snapshot_t snap;
    stark_diag_task_t tasks[EXPORT_TASKS_MAX];
    char module_text[STARK_MODULE_ENTRIES][SELFTEST_MODULE_LINE];
    const char *module_ptr[STARK_MODULE_ENTRIES];
    stark_selftest_row_t rows[STARK_SELFTEST_MAX];
    char tail[SELFTEST_REPORT_TAIL_MAX];
    size_t tail_len;
} export_data_t;

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

typedef struct {
    const selftest_env_t *env;
    stark_file_t *file;
    uint32_t crc;
    size_t bytes;
} sink_t;

static stark_err_t file_sink(void *ctx, const char *s, size_t n)
{
    sink_t *k = ctx;
    stark_err_t err = k->env->storage_write(k->file, s, n);
    if (err == STARK_OK) {
        k->crc = crc32_update(k->crc, (const uint8_t *)s, n);
        k->bytes += n;
    }
    return err;
}

static size_t tail_at(void *ctx, size_t offset, char *buf, size_t max)
{
    const export_data_t *d = ctx;
    if (offset >= d->tail_len) {
        return 0;
    }
    size_t n = d->tail_len - offset < max ? d->tail_len - offset : max;
    memcpy(buf, d->tail + offset, n);
    return n;
}

typedef struct {
    unsigned highest;
} scan_t;

static bool scan_name(const char *name, uint32_t size, void *ctx)
{
    (void)size;
    scan_t *s = ctx;
    unsigned number = 0;
    if (selftest_report_parse(name, &number) && number > s->highest) {
        s->highest = number;
    }
    return true;
}

static void gather(const selftest_env_t *env, export_data_t *d, selftest_report_src_t *src,
                   const char *storage_text)
{
    memset(src, 0, sizeof *src);
    src->version = env->version;
    src->build = env->build;
    src->board = env->board;
    if (env->diag_snapshot(&d->snap) == STARK_OK) {
        src->snap = &d->snap;
        src->uptime_s = d->snap.uptime_s;
    }
    src->tasks = d->tasks;
    src->ntasks = env->diag_tasks(d->tasks, EXPORT_TASKS_MAX);
    size_t modules = env->module_lines(d->module_text, STARK_MODULE_ENTRIES);
    src->nmodules = modules < STARK_MODULE_ENTRIES ? modules : STARK_MODULE_ENTRIES;
    for (size_t i = 0; i < src->nmodules; i++) {
        d->module_ptr[i] = d->module_text[i];
    }
    src->modules = d->module_ptr;
    src->storage = storage_text;
    src->nrows = env->results(d->rows, STARK_SELFTEST_MAX);
    if (src->nrows > STARK_SELFTEST_MAX) {
        src->nrows = STARK_SELFTEST_MAX;
    }
    src->rows = d->rows;
    d->tail_len = env->log_tail(d->tail, sizeof d->tail);
    if (d->tail_len > sizeof d->tail) {
        d->tail_len = sizeof d->tail;
    }
    src->tail = tail_at;
    src->tail_ctx = d;
}

/* Reads the closed file back: its length and CRC-32 must be what the sink saw. */
static bool read_back(const selftest_env_t *env, const char *name, const sink_t *want)
{
    stark_file_t *f = NULL;
    if (env->storage_open(REPORT_DIR, name, STARK_FILE_READ, &f) != STARK_OK) {
        return false;
    }
    uint8_t chunk[READBACK_CHUNK];
    uint32_t crc = 0;
    size_t total = 0;
    bool ok = true;
    for (;;) {
        size_t got = 0;
        if (env->storage_read(f, chunk, sizeof chunk, &got) != STARK_OK) {
            ok = false;
            break;
        }
        if (got == 0u) {
            break;
        }
        crc = crc32_update(crc, chunk, got);
        total += got;
    }
    (void)env->storage_close(f);
    return ok && total == want->bytes && crc == want->crc;
}

stark_err_t selftest_export_run(const selftest_env_t *env, char *name, size_t cap, size_t *bytes)
{
    if (env == NULL || name == NULL || cap < SELFTEST_REPORT_NAME_MAX) {
        return STARK_ERR_INVALID_ARG;
    }
    stark_storage_state_t state = env->storage_state();
    if (state != STARK_STORAGE_MOUNTED) {
        return STARK_ERR_STATE;
    }
    scan_t scan = {0};
    stark_err_t err = env->storage_list(REPORT_DIR, scan_name, &scan);
    if (err != STARK_OK) {
        return err;
    }
    char fname[SELFTEST_REPORT_NAME_MAX];
    if (!selftest_report_name(scan.highest + 1u, fname, sizeof fname)) {
        return STARK_ERR_NO_SPACE;
    }
    export_data_t *d = env->ram_alloc(sizeof *d);
    if (d == NULL) {
        return STARK_ERR_NO_MEM;
    }
    selftest_report_src_t src;
    gather(env, d, &src, env->storage_state_text(state));
    sink_t sink = {.env = env};
    err = env->storage_open(REPORT_DIR, fname, STARK_FILE_WRITE_ATOMIC, &sink.file);
    if (err == STARK_OK) {
        err = selftest_report_write(&src, SELFTEST_REPORT_MAX, SELFTEST_REPORT_TAIL_MAX, file_sink,
                                    &sink, NULL, NULL);
        if (err != STARK_OK) {
            env->storage_discard(sink.file); /* whole or not at all: an atomic file is dropped */
        } else {
            err = env->storage_close(sink.file);
            if (err != STARK_OK) {
                (void)env->storage_remove(REPORT_DIR, fname); /* close may have left a stub */
            }
        }
    }
    env->ram_free(d);
    if (err != STARK_OK) {
        return err;
    }
    if (!read_back(env, fname, &sink)) {
        (void)env->storage_remove(REPORT_DIR, fname);
        return STARK_ERR_IO;
    }
    (void)memcpy(name, fname, strlen(fname) + 1u);
    if (bytes != NULL) {
        *bytes = sink.bytes;
    }
    return STARK_OK;
}
