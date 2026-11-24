/*
 * selftest_report.h — stark_diag (pure): the self-test report as text (TASKS.md STARK-0707,
 * ADR-0023 §8). Pure. A cursor formatter: it renders one line at a time into a small buffer and
 * hands it to a sink, so the report is never held whole, and it stops at a byte cap (16 kB) with a
 * marker instead of overrunning it. Every character it emits is printable ASCII or UTF-8, a
 * newline or nothing: control characters in any field or in the log tail become '.', so a hostile
 * name (a module's descriptor, a log line) cannot inject lines or escape sequences. Nothing is
 * collected that the self-test does not own: no setting value, no file content, no credential.
 *
 * The root (main/selftest_export.c) fills the source from the services and writes the sink to the
 * card; this file knows neither.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_diag.h"

#define SELFTEST_REPORT_MAX      16384u /* bytes in a report */
#define SELFTEST_REPORT_TAIL_MAX 4096u  /* bytes of the log ring's tail in it */
#define SELFTEST_REPORT_NAME_MAX 16u    /* "report_9999.txt" and its NUL */

/* Gets called with the formatted piece; any error stops the report and is returned. */
typedef stark_err_t (*selftest_sink_fn)(void *ctx, const char *s, size_t n);

/* The bytes `n` at `offset` of the tail window (up to max), 0 at its end. The window is what the
 * root copied out of the log ring, whole lines only. */
typedef size_t (*selftest_tail_fn)(void *ctx, size_t offset, char *buf, size_t max);

typedef struct {
    const char *version;
    const char *build;
    const char *board;
    uint32_t uptime_s;
    const stark_diag_snapshot_t *snap; /* NULL: the line says "n/a" */
    const stark_diag_task_t *tasks;
    size_t ntasks;
    const char *const *modules; /* one text line per module-table entry */
    size_t nmodules;
    const char *storage;              /* the storage state, e.g. "mounted" */
    const stark_selftest_row_t *rows; /* every self-test row, run or not */
    size_t nrows;
    selftest_tail_fn tail; /* NULL: no tail section */
    void *tail_ctx;
} selftest_report_src_t;

/*
 * Renders the report into `sink`. `cap` is the most bytes it may emit (SELFTEST_REPORT_MAX in the
 * firmware) and `tail_max` the most of the log tail (SELFTEST_REPORT_TAIL_MAX); a report that
 * would pass `cap` ends with "-- truncated --" inside it, and `*truncated` (optional) says so.
 * `*written` (optional) is the bytes handed to the sink. Returns STARK_OK, the sink's first error,
 * or STARK_ERR_INVALID_ARG for a NULL source or sink or a cap below 64.
 */
stark_err_t selftest_report_write(const selftest_report_src_t *src, size_t cap, size_t tail_max,
                                  selftest_sink_fn sink, void *sink_ctx, size_t *written,
                                  bool *truncated);

/* "report_NNNN.txt" for number 0..9999; false (and an empty string) past that. */
bool selftest_report_name(unsigned number, char *buf, size_t cap);

/* True and the number if `name` is exactly "report_NNNN.txt" with four digits. */
bool selftest_report_parse(const char *name, unsigned *number);
