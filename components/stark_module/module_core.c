/*
 * module_core.c — discovery and capability state (module_core.h). Pure.
 */
#include "module_core.h"
#include <string.h>

/* An exclusive capability can have only one provider on the bus (the PN532
 * has one fixed address). */
#define CAPS_EXCLUSIVE STARK_CAP_NFC
/* These say how a slot is wired; they enable a claim, never an app. */
#define CAPS_WIRING    (STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ)

static void lock(module_core_t *c)
{
    if (c->io.lock != NULL) {
        c->io.lock(c->io.ctx);
    }
}

static void unlock(module_core_t *c)
{
    if (c->io.unlock != NULL) {
        c->io.unlock(c->io.ctx);
    }
}

/* FNV-1a: tells two reads apart without keeping a second copy of them. */
static uint32_t hash32(const uint8_t *b, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

static void clear_entry(module_slot_t *s)
{
    uint8_t index = s->e.slot;
    memset(&s->e, 0, sizeof s->e);
    s->e.kind = STARK_MODULE_SLOT;
    s->e.state = STARK_MODULE_ABSENT;
    s->e.slot = index;
    s->valid = false;
}

static void make_generic(module_slot_t *s, module_desc_result_t why)
{
    clear_entry(s);
    s->e.state = STARK_MODULE_GENERIC;
    s->e.why = why;
}

static void accept(module_slot_t *s, const uint8_t *raw)
{
    module_desc_t d;
    module_desc_result_t r = module_desc_parse(raw, MODULE_DESC_SIZE, &d);
    if (r != MODULE_DESC_OK) {
        make_generic(s, r);
        return;
    }
    clear_entry(s);
    s->e.id = d.id;
    s->e.rev = d.rev;
    s->e.declared = d.caps;
    memcpy(s->e.name, d.name, sizeof s->e.name);
    if (module_desc_caps_supported(d.caps)) {
        s->e.state = STARK_MODULE_KNOWN;
        s->valid = true;
    } else {
        s->e.state = STARK_MODULE_GENERIC;
        s->e.unsupported = true;
    }
}

/* Marks conflicting slots and returns the capabilities they withhold. */
static uint32_t resolve_conflicts(module_core_t *c)
{
    uint32_t withheld = 0;
    for (size_t i = 0; i < c->n; i++) {
        if (c->slot[i].valid) {
            c->slot[i].e.state = STARK_MODULE_KNOWN;
        }
    }
    for (size_t i = 0; i < c->n; i++) {
        module_slot_t *a = &c->slot[i];
        for (size_t j = i + 1; a->valid && j < c->n; j++) {
            module_slot_t *b = &c->slot[j];
            if (!b->valid) {
                continue;
            }
            uint32_t ex = (a->e.declared | b->e.declared) & CAPS_EXCLUSIVE;
            uint32_t both = a->e.declared & b->e.declared & CAPS_EXCLUSIVE;
            if (both != 0 || (a->e.id == b->e.id && ex != 0)) {
                a->e.state = STARK_MODULE_CONFLICT;
                b->e.state = STARK_MODULE_CONFLICT;
                withheld |= ex;
            }
        }
    }
    return withheld;
}

/* With the lock held. */
static bool recompute(module_core_t *c)
{
    uint32_t withheld = resolve_conflicts(c);
    uint32_t caps = c->confirmed & ~withheld & STARK_CAPS_KNOWN & ~(uint32_t)CAPS_WIRING;
    bool changed = caps != c->caps;
    c->caps = caps;
    return changed;
}

void module_core_init(module_core_t *c, const module_io_t *io, const uint8_t *addrs, size_t n)
{
    memset(c, 0, sizeof *c);
    c->io = *io;
    c->n = n < STARK_MODULE_SLOTS ? n : STARK_MODULE_SLOTS;
    for (size_t i = 0; i < c->n; i++) {
        c->slot[i].addr = addrs[i];
        c->slot[i].e.slot = (uint8_t)i;
        clear_entry(&c->slot[i]);
    }
}

/* One slot's work after its bus calls; true if the entry changed. */
static bool apply(module_slot_t *s, stark_err_t probe, stark_err_t read, const uint8_t *raw)
{
    if (probe != STARK_OK) {
        s->seen = false;
        s->read_errors = 0;
        if (s->misses < MODULE_MISS_LIMIT) {
            s->misses++;
        }
        if (s->misses < MODULE_MISS_LIMIT || s->e.state == STARK_MODULE_ABSENT) {
            return false;
        }
        s->accepted = false;
        clear_entry(s);
        return true;
    }
    s->misses = 0;
    if (read != STARK_OK) {
        s->seen = false;
        if (s->read_errors < MODULE_READ_LIMIT) {
            s->read_errors++;
        }
        if (s->e.state != STARK_MODULE_ABSENT || s->read_errors < MODULE_READ_LIMIT) {
            return false; /* a known entry survives; a new slot gets a few tries */
        }
        make_generic(s, MODULE_DESC_TRUNCATED);
        return true;
    }
    s->read_errors = 0;
    uint32_t h = hash32(raw, MODULE_DESC_SIZE);
    bool stable = s->seen && h == s->seen_hash;
    s->seen = true;
    s->seen_hash = h;
    if (!stable || (s->accepted && h == s->accepted_hash)) {
        return false;
    }
    s->accepted = true;
    s->accepted_hash = h;
    accept(s, raw);
    return true;
}

uint32_t module_core_tick(module_core_t *c)
{
    uint32_t changed = 0;
    for (size_t i = 0; i < c->n; i++) {
        module_slot_t *s = &c->slot[i];
        uint8_t raw[MODULE_DESC_SIZE];
        stark_err_t read = STARK_ERR_STATE;
        stark_err_t probe = c->io.probe(c->io.ctx, s->addr);
        if (probe == STARK_OK) {
            read = c->io.read(c->io.ctx, s->addr, 0, raw, sizeof raw);
        }
        lock(c);
        if (apply(s, probe, read, raw)) {
            changed |= UINT32_C(1) << i;
        }
        unlock(c);
    }
    if (changed != 0) {
        lock(c);
        (void)recompute(c);
        unlock(c);
    }
    return changed;
}

bool module_core_confirm(module_core_t *c, uint32_t caps, bool present)
{
    lock(c);
    if (present) {
        c->confirmed |= caps;
    } else {
        c->confirmed &= ~caps;
    }
    bool changed = recompute(c);
    unlock(c);
    return changed;
}

uint32_t module_core_caps(module_core_t *c)
{
    lock(c);
    uint32_t caps = c->caps;
    unlock(c);
    return caps;
}

size_t module_core_list(module_core_t *c, stark_module_entry_t *out, size_t max)
{
    lock(c);
    for (size_t i = 0; out != NULL && i < c->n && i < max; i++) {
        out[i] = c->slot[i].e;
        out[i].confirmed = c->slot[i].e.declared & c->confirmed;
    }
    unlock(c);
    return c->n;
}
