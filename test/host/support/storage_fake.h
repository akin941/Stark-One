/*
 * storage_fake.h — test-only stand-in for the stark_storage file API (stark_storage.h): files in
 * memory in the four directories, a card state the test sets, and failures to inject. Unlike the
 * UI port's storage (ui_port_storage.h: captures/ only, 2 kB, one text file) it holds a megabyte
 * and counts closes (a close is what syncs a file to the medium), so the UART log's buffering, its
 * syncs, its limit and a card that goes away mid-log can be checked. Only test files link it.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "stark_storage.h"

/* Everything back to the start: card MOUNTED, no files, no failure. */
void sfake_reset(void);
void sfake_set_state(stark_storage_state_t state);

/* A file in `dir` (replaces one of the same name). */
void sfake_put(stark_dir_t dir, const char *name, const uint8_t *data, size_t n);

/* The content of a file, NULL when there is none; `*n` its length. */
const uint8_t *sfake_get(stark_dir_t dir, const char *name, size_t *n);
size_t sfake_files(stark_dir_t dir);

/* The next call of this kind, after `after` more that succeed, fails with STARK_ERR_IO until
 * cleared (after < 0). */
typedef enum {
    SFAKE_LIST,
    SFAKE_OPEN,
    SFAKE_READ,
    SFAKE_WRITE,
    SFAKE_CLOSE,
    SFAKE_KINDS
} sfake_call_t;
void sfake_fail(sfake_call_t call, int after);

/* What the code under test did: handles open now, closes and writes since the reset, the state
 * `stark_storage_state()` reports. */
unsigned sfake_open_handles(void);
unsigned sfake_closes(void);
unsigned sfake_writes(void);
