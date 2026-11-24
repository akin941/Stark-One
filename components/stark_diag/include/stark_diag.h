/*
 * stark_diag.h — L3, diagnostics v1 (TASKS.md STARK-0109, ARCHITECTURE.md
 * §6.9): heap, task stack high-water marks, FPS, frame overruns and event
 * drops, measured in one place, logged, and shown by the Diagnostics app.
 * Local only — nothing leaves the device. The self-test (STARK-0707) is at the end.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

/* The UI's counters, which L3 cannot read itself (stark_ui/stark_app are
 * L4): main injects a source that fills them (stark_diag_init()). */
typedef struct {
    uint32_t frames;
    uint32_t overruns;
    uint32_t render_errors;
    uint32_t join_timeouts;
} stark_diag_ui_t;

typedef void (*stark_diag_ui_fn)(stark_diag_ui_t *out);

typedef struct {
    uint32_t heap_free;    /* free internal heap, bytes */
    uint32_t heap_min;     /* minimum ever free (the allocator tracks it) */
    uint32_t heap_largest; /* largest free block */
    uint32_t fps_x10;      /* frames per second x 10, over the last sampling window */
    stark_diag_ui_t ui;
    uint32_t ev_published;
    uint32_t ev_dropped;
    uint32_t ev_max_depth;
    uint32_t uptime_s;
} stark_diag_snapshot_t;

typedef struct {
    char name[16];
    uint32_t stack_free; /* high-water mark: the least free stack seen, bytes */
} stark_diag_task_t;

/* STARK_EVT_SYSTEM system.id published after every sample (once a second);
 * the Diagnostics app refreshes on it. */
#define STARK_SYS_DIAG_SAMPLE 1u

/*
 * Takes a silent first sample (the snapshot is valid at once), then starts
 * the 1 s sampler (an esp_timer). Each timer sample computes FPS from the
 * frame counter, refreshes the snapshot and publishes STARK_SYS_DIAG_SAMPLE;
 * the first one and then one every CONFIG_STARK_DIAG_LOG_PERIOD_S (10; 0 =
 * never) also log the line
 *     diag: heap=<n> min=<n> fps=<n.n> drops=<n> overruns=<n>
 * ui_source may be NULL (UI counters stay 0). Needs stark_event_init().
 * STARK_ERR_STATE if already started.
 */
stark_err_t stark_diag_init(stark_diag_ui_fn ui_source);

/* The latest sample (any task). STARK_ERR_INVALID_ARG for NULL,
 * STARK_ERR_STATE before init. */
stark_err_t stark_diag_snapshot(stark_diag_snapshot_t *out);

/* Name and stack high-water mark of up to max tasks; returns how many
 * were written. */
size_t stark_diag_tasks(stark_diag_task_t *out, size_t max);

/* ---- self-test (TASKS.md STARK-0707, ADR-0023 §8) ------------------------
 *
 * A check table registered by the composition root (main/selftest_checks.c, so stark_diag needs no
 * new edge), a runner that bounds and records each check, and a result table the Self-Test app
 * reads. An operator-attested result is never ST_PASS: the device cannot see or hear itself.
 * Everything stays on the device (SECURITY_SCOPE §2). */
typedef enum {
    ST_NOT_RUN,
    ST_PASS,
    ST_FAIL,
    ST_SKIP,
    ST_ATTESTED,
} stark_selftest_result_t;

#define STARK_SELFTEST_MAX      12u /* checks in a table */
#define STARK_SELFTEST_EVIDENCE 48u /* characters of evidence, a line, no control characters */

/*
 * One check. `run` writes at most cap - 1 characters of evidence (a one-line string) and returns
 * ST_PASS, ST_FAIL or ST_SKIP; anything else is a failure ("bad result"). It runs on the caller
 * (the app worker) and must be bounded and free of side effects. A NULL `run` is an interactive
 * check: the app observes it and records the result with stark_selftest_report(). `attested`
 * marks what only the operator can judge.
 */
typedef struct {
    const char *name; /* "RAM", "Flash", ... shown and logged (lower-cased) */
    bool attested;
    stark_selftest_result_t (*run)(char *evidence, size_t cap);
} stark_selftest_check_t;

typedef struct {
    const char *name;
    bool attested;
    stark_selftest_result_t result;
    char evidence[STARK_SELFTEST_EVIDENCE + 1u];
    uint32_t ms; /* how long the check took (0 for a reported result) */
} stark_selftest_row_t;

/* STARK_EVT_SYSTEM system.id; arg is STARK_SELFTEST_EVT_STEP after every recorded result and
 * STARK_SELFTEST_EVT_DONE once no row is left at ST_NOT_RUN. */
#define STARK_SYS_SELFTEST      9u
#define STARK_SELFTEST_EVT_STEP 1
#define STARK_SELFTEST_EVT_DONE 2

/* One registration, at most STARK_SELFTEST_MAX checks; the table must outlive the firmware.
 * STARK_ERR_INVALID_ARG (NULL, 0, too many, a NULL name), STARK_ERR_STATE (twice, or before
 * stark_diag_init()). */
stark_err_t stark_selftest_register(const stark_selftest_check_t *table, size_t n);

/* Runs one check on the caller and records it (as stark_selftest_report()). A check
 * that takes longer than 10 s is recorded ST_FAIL "timeout". STARK_ERR_INVALID_ARG for an index
 * past the table, STARK_ERR_NOT_SUPPORTED for an interactive check, STARK_ERR_STATE before
 * registration. */
stark_err_t stark_selftest_run(size_t index);

/* Records a result the app observed; evidence is truncated to 48 characters and stripped of control
 * characters. ST_NOT_RUN clears a row, silently (no log line, no event). An index past the table is
 * ignored. The log line is `selftest: <name> <result> <evidence>` and, once no row is left at
 * ST_NOT_RUN, `selftest: summary 7 pass  3 attested  0 fail  0 skip`. */
void stark_selftest_report(size_t index, stark_selftest_result_t r, const char *evidence);

/* Copies up to max rows and returns how many checks are registered. */
size_t stark_selftest_results(stark_selftest_row_t *out, size_t max);

/*
 * The report export, installed by the composition root (main/selftest_export.c: the root alone
 * knows the module table, the board and the card). Writes `logs/report_NNNN.txt` (at most 16 kB,
 * whole or not at all, read back and compared before it is reported good), copies the file name
 * into `name` and the size into `*bytes`. STARK_ERR_STATE when no exporter is installed or no card
 * is mounted, any storage error otherwise (no file is left behind). Runs on the caller (a worker:
 * it blocks on the card).
 */
typedef stark_err_t (*stark_selftest_export_fn)(char *name, size_t cap, size_t *bytes);
void stark_selftest_set_export(stark_selftest_export_fn fn);
stark_err_t stark_selftest_export(char *name, size_t cap, size_t *bytes);
