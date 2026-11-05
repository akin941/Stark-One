/*
 * module_core.h — discovery and capability state (TASKS.md STARK-0502). Pure,
 * over an injected I/O table; the host tests run it against the fake 24C02.
 * Bus calls are made with the lock released, so a slow bus never blocks a
 * reader of the table.
 */
#pragma once

#include "stark_module.h"

#define MODULE_MISS_LIMIT 2u /* consecutive failed probes that remove a module */
#define MODULE_READ_LIMIT 3u /* consecutive read errors that make a new slot generic */

typedef struct {
    stark_err_t (*probe)(void *ctx, uint8_t addr);
    stark_err_t (*read)(void *ctx, uint8_t addr, uint8_t off, uint8_t *buf, size_t len);
    void (*lock)(void *ctx);   /* optional */
    void (*unlock)(void *ctx); /* optional */
    void *ctx;
} module_io_t;

typedef struct {
    stark_module_entry_t e;
    uint8_t addr;
    uint8_t misses;
    uint8_t read_errors;
    bool valid;    /* a parsed, supported descriptor (conflict is judged from these) */
    bool seen;     /* seen_hash holds the previous read */
    bool accepted; /* accepted_hash holds the content e came from */
    uint32_t seen_hash;
    uint32_t accepted_hash;
} module_slot_t;

typedef struct {
    module_io_t io;
    module_slot_t slot[STARK_MODULE_SLOTS];
    size_t n;
    uint32_t confirmed; /* what drivers have confirmed */
    uint32_t caps;      /* effective */
} module_core_t;

void module_core_init(module_core_t *c, const module_io_t *io, const uint8_t *addrs, size_t n);

/*
 * One polling round: at most one probe and one read per slot. Returns the
 * mask of slots whose entry changed.
 */
uint32_t module_core_tick(module_core_t *c);

/* True if the effective capabilities changed. */
bool module_core_confirm(module_core_t *c, uint32_t caps, bool present);
uint32_t module_core_caps(module_core_t *c);
size_t module_core_list(module_core_t *c, stark_module_entry_t *out, size_t max);
