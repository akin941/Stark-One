/*
 * selftest_env_fake.h — host test support: a selftest_env_t over a model of the machine that a
 * test can make healthy or break one call at a time (TASKS.md STARK-0707): a heap with a floor, a
 * RAM that can hold a stuck bit, a flash holding a synthetic image (reads only: the model has no
 * write or erase to call), settings, a card with named files and a failure at any step, a module
 * table with probes, the diagnostics snapshot and task table. Test-only.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "selftest_env.h"

#define FENV_FLASH_BYTES (1024u * 1024u)
#define FENV_APP_OFFSET  0x10000u
#define FENV_APP_SIZE    0x80000u
#define FENV_FILES       8
#define FENV_FILE_BYTES  20000u
#define FENV_NAME        32

/* Where the fake card fails (the first call of that kind, once armed). */
typedef enum {
    FENV_OK,
    FENV_OPEN_WRITE,
    FENV_WRITE, /* the n-th write (fenv.fail_nth, 0 = the first) */
    FENV_CLOSE,
    FENV_OPEN_READ,
    FENV_READ,
    FENV_SHORT_READ, /* the file ends early */
    FENV_BAD_READ,   /* the data read differs */
    FENV_REMOVE,
    FENV_LIST
} fenv_card_fault_t;

typedef struct {
    char name[FENV_NAME];
    uint8_t data[FENV_FILE_BYTES];
    size_t len;
    bool used;
} fenv_file_t;

typedef struct {
    /* RAM */
    size_t free_total; /* what the heap holds before the test block */
    size_t largest;
    size_t allocated; /* bytes currently out */
    unsigned allocs, frees;
    size_t last_alloc;
    size_t min_free_seen; /* the lowest the heap went (free_total - allocated) */
    bool alloc_fails;
    size_t steal_on_alloc;  /* another task takes this much heap while the block is out */
    size_t stuck_offset;    /* ram_dwell flips the bits in stuck_mask at this offset ... */
    uint8_t stuck_mask;     /* ... (0: healthy) */
    unsigned stuck_on_call; /* only on this dwell (1-based; 0: every dwell) */
    unsigned dwells;
    /* flash */
    uint8_t flash[FENV_FLASH_BYTES];
    uint32_t chip_size;
    bool chip_size_fails, app_fails;
    uint32_t read_fail_at; /* a read touching this offset fails (UINT32_MAX: none) */
    uint32_t reads, bytes_read, max_offset_read;
    uint32_t corrupt_after_reads; /* then every read returns a flipped first byte (0: never) */
    /* settings */
    uint8_t setting[STARK_SET_COUNT];
    stark_settings_source_t source;
    bool no_desc;
    unsigned get_calls;
    bool unstable; /* a get returns a different value each time */
    /* card */
    stark_storage_state_t state;
    fenv_card_fault_t fault;
    unsigned fail_nth;
    unsigned writes, opens, closes, discards, removes;
    int handles_open; /* handles opened and not yet closed or discarded */
    fenv_file_t files[FENV_FILES];
    /* modules */
    stark_module_entry_t entries[STARK_MODULE_ENTRIES];
    size_t n_entries;
    stark_err_t probe_result[2]; /* per slot: what the probe of its address returns */
    bool no_eeprom_addr;
    unsigned probes, lists;
    unsigned change_after_list; /* the table gains a module after this many list calls (0: never) */
    /* diagnostics */
    stark_diag_snapshot_t snap;
    bool snap_fails;
    stark_diag_task_t tasks[16];
    size_t n_tasks;
    /* report */
    char tail[6000];
    size_t tail_len;
    size_t tail_overreport;    /* log_tail claims this many bytes more than it copied */
    size_t list_overreport;    /* module_list claims this many entries more than it copied */
    size_t modules_overreport; /* module_lines claims this many lines more than it wrote */
    stark_selftest_row_t rows[STARK_SELFTEST_MAX];
    size_t n_rows;
} fenv_t;

extern fenv_t fenv;
extern selftest_env_t fenv_env;

/* A healthy machine: 226 kB free, 4 MB flash with a valid bootloader header and a valid image in
 * the app partition, four valid settings, a mounted card, an absent slot 0 and slot 1 and the
 * matching probes, no drops and roomy stacks. Resets every counter. */
void fenv_reset(void);

/* The synthetic app image: header, segments, padding, checksum and (if `digest`) 32 appended
 * bytes, written at FENV_APP_OFFSET; returns its length. */
uint32_t fenv_put_image(unsigned segments, uint32_t segment_bytes, bool digest);

/* The file named `name` or NULL. */
fenv_file_t *fenv_find(const char *name);
unsigned fenv_file_count(void);
