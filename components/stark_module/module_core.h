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

/* One slot's wiring (stark_board_module_slot_t, injected so that a two-slot
 * board can be tested on a board table that has one). */
typedef struct {
    uint8_t addr;
    int cs, irq0, irq1;
} module_wiring_t;

typedef struct {
    stark_module_entry_t e;
    module_wiring_t wire;
    const char *owner; /* holds the claim; NULL when free */
    uint32_t claimed;  /* the lines it holds */
    uint32_t gen;      /* changes on removal or replacement: old leases go stale */
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
    struct {
        stark_module_kind_t kind;
        const char *name;
        uint32_t caps;
    } provider[STARK_MODULE_ENTRIES - STARK_MODULE_SLOTS];
    size_t n_providers;
    uint32_t confirmed; /* what drivers have confirmed */
    uint32_t caps;      /* effective */
} module_core_t;

void module_core_init(module_core_t *c, const module_io_t *io, const module_wiring_t *wires,
                      size_t n);

/*
 * One polling round: at most one probe and one read per slot. Returns the
 * mask of slots whose entry changed.
 */
uint32_t module_core_tick(module_core_t *c);

/* True if the effective capabilities changed. */
bool module_core_confirm(module_core_t *c, uint32_t caps, bool present);
uint32_t module_core_caps(module_core_t *c);
void module_core_provide(module_core_t *c, stark_module_kind_t kind, const char *name,
                         uint32_t caps);
/* Copies up to max entries: the slots, then the providers; returns how many exist. */
size_t module_core_list(module_core_t *c, stark_module_entry_t *out, size_t max);

/* stark_module.h's arbitration, purely. */
stark_err_t module_core_claim(module_core_t *c, uint8_t slot, uint32_t caps, const char *owner,
                              stark_module_lease_t *out);
void module_core_release(module_core_t *c, const stark_module_lease_t *l);
bool module_core_lease_valid(module_core_t *c, const stark_module_lease_t *l);
stark_err_t module_core_lease_pins(module_core_t *c, const stark_module_lease_t *l, int *cs,
                                   int *irq0, int *irq1);
