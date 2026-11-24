/*
 * selftest_core.h — private to stark_diag: the self-test runner (TASKS.md STARK-0707, ADR-0023
 * §8). Pure: no ESP-IDF header, no clock, lock, log or event of its own — everything it needs is
 * injected (selftest_io_t), so the host tests drive it with a clock they advance and a notifier
 * that records. stark_diag.c binds one instance to esp_timer, a FreeRTOS mutex, the log and the
 * event bus; test/host/port/ui_port_selftest.c binds another to the UI port.
 *
 * What the runner promises:
 *   - every result is one of the five of stark_selftest_result_t; a check that returns anything
 *     else is recorded ST_FAIL "bad result";
 *   - a check that took longer than SELFTEST_DEADLINE_MS is recorded ST_FAIL "timeout" whatever
 *     it returned (a check cannot be preempted: the runner judges it when it returns, and every
 *     check keeps its own loops bounded);
 *   - evidence is at most 48 characters and holds no control character;
 *   - a run that never returned (the contained app faulted inside a check) leaves its row at
 *     ST_NOT_RUN, and the next run records it ST_FAIL "app_fault": the table keeps what finished.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_diag.h"

#define SELFTEST_DEADLINE_MS 10000u
#define SELFTEST_NONE        SIZE_MAX

typedef struct {
    uint32_t (*now_ms)(void *ctx); /* a free-running millisecond clock; wrapping is handled */
    void (*lock)(void *ctx);       /* optional: guards the table against a reader on another task */
    void (*unlock)(void *ctx);
    /* Called after a result was recorded (not after a row was cleared), outside the lock; `summary`
     * is NULL until no row is left at ST_NOT_RUN, then the text of selftest_summary(). */
    void (*notify)(void *ctx, size_t index, const stark_selftest_row_t *row, const char *summary);
    void *ctx;
} selftest_io_t;

typedef struct {
    selftest_io_t io;
    const stark_selftest_check_t *table;
    size_t n;
    stark_selftest_row_t rows[STARK_SELFTEST_MAX];
    size_t in_flight; /* the check being run, or SELFTEST_NONE */
} selftest_t;

void selftest_init(selftest_t *t, const selftest_io_t *io);

stark_err_t selftest_register(selftest_t *t, const stark_selftest_check_t *table, size_t n);

stark_err_t selftest_run(selftest_t *t, size_t index);

/* STARK_ERR_INVALID_ARG for an index past the table or a result that is not a
 * stark_selftest_result_t; STARK_ERR_STATE before registration. */
stark_err_t selftest_report_result(selftest_t *t, size_t index, stark_selftest_result_t r,
                                   const char *evidence);

/* Copies up to max rows; returns how many checks are registered. */
size_t selftest_results(selftest_t *t, stark_selftest_row_t *out, size_t max);

/* "7 pass  3 attested  0 fail  0 skip", plus "  N not run" when N > 0; always NUL-terminated. */
void selftest_summary(const stark_selftest_row_t *rows, size_t n, char *buf, size_t cap);

/* The lower-case name of a result: "not run", "pass", "fail", "skip", "attested". */
const char *selftest_result_name(stark_selftest_result_t r);
