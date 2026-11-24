/*
 * selftest_checks.h — the ten self-test checks (TASKS.md STARK-0707, ADR-0023 §8), pure over the
 * environment table of selftest_env.h. Registered at the composition root so stark_diag needs no
 * new edge. The table holds, in this order: RAM, Flash, Settings, Storage, Module bus, Events and
 * stacks (automated: they run on the app worker), Buttons (observed by the app from the input
 * service's key events), Display, Buzzer and Status LED (attested by the operator). The last four
 * have no `run`: the Self-Test app walks the operator through them and reports the result.
 *
 * Every automated check is bounded (a fixed number of reads, probes or pattern passes, a block of
 * at most 32 kB, an I2C probe of 5 ms), free of side effects (the flash is only read, the card
 * file is removed again) and writes at most 48 characters of evidence.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "selftest_env.h"

/* The checks' places in the table and in the app's walk. */
enum {
    SELFTEST_RAM,
    SELFTEST_FLASH,
    SELFTEST_SETTINGS,
    SELFTEST_STORAGE,
    SELFTEST_MODULES,
    SELFTEST_EVENTS,
    SELFTEST_BUTTONS,
    SELFTEST_DISPLAY,
    SELFTEST_BUZZER,
    SELFTEST_LED,
    SELFTEST_COUNT,
    SELFTEST_AUTOMATED = SELFTEST_BUTTONS /* the first check the app observes itself */
};

#define SELFTEST_RAM_BLOCK_MAX    32768u  /* the largest block the RAM check tests */
#define SELFTEST_HEAP_FLOOR       131072u /* free heap never goes below this because of it */
#define SELFTEST_STACK_MIN        256u    /* every task keeps at least this much stack free */
#define SELFTEST_STORAGE_BYTES    4096u   /* the card check's temp file */
#define SELFTEST_FLASH_CHUNK      256u    /* bytes per flash read */
#define SELFTEST_PROBE_TIMEOUT_MS 5u      /* an address probe */

/* Binds the environment every check reads (it must outlive the checks). */
void selftest_checks_bind(const selftest_env_t *env);

/* The registration table (SELFTEST_COUNT entries) and its length. */
const stark_selftest_check_t *selftest_checks_table(size_t *n);

/* The automated checks, for the host tests; each one is also the table's `run`. */
stark_selftest_result_t selftest_check_ram(char *evidence, size_t cap);
stark_selftest_result_t selftest_check_flash(char *evidence, size_t cap);
stark_selftest_result_t selftest_check_settings(char *evidence, size_t cap);
stark_selftest_result_t selftest_check_storage(char *evidence, size_t cap);
stark_selftest_result_t selftest_check_modules(char *evidence, size_t cap);
stark_selftest_result_t selftest_check_events(char *evidence, size_t cap);

/* The length of the application image that starts at `offset` (header, segments, padding, the
 * checksum byte and the appended digest), read through env->flash_read. STARK_ERR_INVALID_ARG for
 * anything that is not an image or does not fit `limit` bytes. Exposed for the host tests. */
stark_err_t selftest_image_length(const selftest_env_t *env, uint32_t offset, uint32_t limit,
                                  uint32_t *length);
