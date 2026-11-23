/*
 * lab_pins.c — the lab service's pad decisions (lab_pins.h). Pure: no lock, no allocation, no
 * ESP-IDF header. Every operation bounds its own work: a table of at most
 * STARK_BOARD_LAB_PINS_MAX pins and a fixed number of transport calls.
 */
#include "lab_pins.h"
#include <string.h>
#include "stark_lab_transport.h"

/* The table index of a GPIO number, or -1 for every number the board does not offer. */
static int find(const lab_pins_t *p, int pin)
{
    for (size_t i = 0; i < p->n; i++) {
        if (p->tab[i].gpio == pin) {
            return (int)i;
        }
    }
    return -1;
}

/* Whether the pin is offered right now: its class and, for a slot's line, the module table. */
static bool offered_now(lab_pins_t *p, size_t i)
{
    const stark_board_lab_pin_t *t = &p->tab[i];
    if (t->cls != STARK_PIN_SLOT) {
        return stark_lab_pin_offered(t->cls, false);
    }
    stark_module_entry_t list[STARK_MODULE_ENTRIES];
    size_t n = p->io.list(p->io.ctx, list, STARK_MODULE_ENTRIES);
    if (n > STARK_MODULE_ENTRIES) {
        n = STARK_MODULE_ENTRIES;
    }
    return stark_lab_pin_offered(t->cls, stark_lab_slot_absent(list, n, t->slot));
}

/* The pin stops being offered: released to hi-Z if held, logged if it had been offered. */
static void lose(lab_pins_t *p, size_t i)
{
    if (p->hold[i].held) {
        (void)p->io.pad_hiz(p->io.ctx, p->tab[i].gpio);
        memset(&p->hold[i], 0, sizeof(p->hold[i]));
        p->changed = true;
    }
    if ((p->offered & (1u << i)) != 0u) {
        p->offered &= ~(1u << i);
        p->io.log(p->io.ctx, LAB_LOG_WITHDRAWN, p->tab[i].gpio, p->tab[i].slot);
        p->changed = true;
    }
}

/*
 * The guard every pin call starts with: the offer first (a pin that is not offered touches
 * nothing, and a held one is withdrawn), then the hold. On STARK_OK, *idx is the table index.
 */
static stark_err_t guard(lab_pins_t *p, int pin, size_t *idx)
{
    int found = find(p, pin);
    if (found < 0) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    *idx = (size_t)found;
    if (!offered_now(p, *idx)) {
        lose(p, *idx);
        return STARK_ERR_NOT_SUPPORTED;
    }
    return p->hold[*idx].held ? STARK_OK : STARK_ERR_STATE;
}

/* A failed operation: the pin is released to hi-Z, its arm goes, the error is passed on. */
static stark_err_t fail(lab_pins_t *p, size_t i, stark_err_t err)
{
    (void)p->io.pad_hiz(p->io.ctx, p->tab[i].gpio);
    p->hold[i].armed = false;
    return err;
}

void lab_pins_init(lab_pins_t *p, const lab_io_t *io, const stark_board_pins_t *pins)
{
    memset(p, 0, sizeof(*p));
    p->io = *io;
    p->n = stark_board_lab_pins_of(pins, p->tab, STARK_BOARD_LAB_PINS_MAX);
    p->offered = lab_pins_offered_mask(p);
}

uint32_t lab_pins_offered_mask(lab_pins_t *p)
{
    uint32_t mask = 0;
    for (size_t i = 0; i < p->n; i++) {
        if (offered_now(p, i)) {
            mask |= 1u << i;
        }
    }
    return mask;
}

size_t lab_pins_list(lab_pins_t *p, stark_lab_pin_t *out, size_t max)
{
    for (size_t i = 0; i < p->n && out != NULL && i < max; i++) {
        out[i] = (stark_lab_pin_t){.pin = p->tab[i].gpio,
                                   .name = p->tab[i].name,
                                   .cls = p->tab[i].cls,
                                   .offered = offered_now(p, i),
                                   .held = p->hold[i].held};
    }
    return p->n;
}

stark_err_t lab_pins_acquire(lab_pins_t *p, int pin, const char *owner)
{
    int i = find(p, pin);
    if (i < 0 || !offered_now(p, (size_t)i)) {
        if (i >= 0) {
            lose(p, (size_t)i);
        }
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (owner == NULL || owner[0] == '\0') {
        return STARK_ERR_INVALID_ARG;
    }
    if (p->hold[i].held) {
        return STARK_ERR_BUSY;
    }
    stark_err_t err = p->io.pad_hiz(p->io.ctx, pin);
    if (err != STARK_OK) {
        return err;
    }
    memset(&p->hold[i], 0, sizeof(p->hold[i]));
    p->hold[i].held = true;
    strncpy(p->hold[i].owner, owner, LAB_OWNER_MAX - 1u);
    return STARK_OK;
}

void lab_pins_release(lab_pins_t *p, int pin)
{
    int i = find(p, pin);
    if (i >= 0 && p->hold[i].held) {
        /* NOTE: no offer check: releasing is always safe, and a withdrawn pin must still reach hi-Z
         */
        (void)p->io.pad_hiz(p->io.ctx, pin);
        memset(&p->hold[i], 0, sizeof(p->hold[i]));
    }
}

void lab_pins_release_all(lab_pins_t *p)
{
    for (size_t i = 0; i < p->n; i++) {
        lab_pins_release(p, p->tab[i].gpio);
    }
}

stark_err_t lab_pins_arm(lab_pins_t *p, int pin)
{
    size_t i;
    stark_err_t gerr = guard(p, pin, &i);
    if (gerr != STARK_OK) {
        return gerr;
    }
    p->hold[i].armed = true;
    return STARK_OK;
}

stark_err_t lab_pins_input(lab_pins_t *p, int pin, stark_lab_pull_t pull)
{
    size_t i;
    stark_err_t gerr = guard(p, pin, &i);
    if (gerr != STARK_OK) {
        return gerr;
    }
    if (pull != STARK_LAB_PULL_NONE && pull != STARK_LAB_PULL_UP && pull != STARK_LAB_PULL_DOWN) {
        return STARK_ERR_INVALID_ARG;
    }
    stark_err_t err = p->io.pad_in(p->io.ctx, pin, (uint8_t)pull);
    return err == STARK_OK ? STARK_OK : fail(p, i, err);
}

stark_err_t lab_pins_read(lab_pins_t *p, int pin, bool *level)
{
    size_t i;
    stark_err_t gerr = guard(p, pin, &i);
    if (gerr != STARK_OK) {
        return gerr;
    }
    if (level == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    stark_err_t err = p->io.pad_read(p->io.ctx, pin, level);
    return err == STARK_OK ? STARK_OK : fail(p, i, err);
}

stark_err_t lab_pins_state(lab_pins_t *p, int pin, stark_lab_pinstate_t *st)
{
    size_t i;
    stark_err_t gerr = guard(p, pin, &i);
    if (gerr != STARK_OK) {
        return gerr;
    }
    if (st == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    bool up = false;
    bool down = false;
    stark_err_t err = p->io.pad_in(p->io.ctx, pin, STARK_LAB_PAD_UP);
    if (err == STARK_OK) {
        p->io.delay_us(p->io.ctx, LAB_PROBE_SETTLE_US);
        err = p->io.pad_read(p->io.ctx, pin, &up);
    }
    if (err == STARK_OK) {
        err = p->io.pad_in(p->io.ctx, pin, STARK_LAB_PAD_DOWN);
    }
    if (err == STARK_OK) {
        p->io.delay_us(p->io.ctx, LAB_PROBE_SETTLE_US);
        err = p->io.pad_read(p->io.ctx, pin, &down);
    }
    if (err == STARK_OK) {
        err = p->io.pad_in(p->io.ctx, pin, STARK_LAB_PAD_NONE);
    }
    if (err != STARK_OK) {
        return fail(p, i, err);
    }
    if (up && !down) {
        *st = STARK_LAB_PIN_FLOATING;
    } else if (up && down) {
        *st = STARK_LAB_PIN_HIGH;
    } else if (!up && !down) {
        *st = STARK_LAB_PIN_LOW;
    } else {
        return fail(p, i, STARK_ERR_IO); /* low with the pull-up, high with the pull-down */
    }
    return STARK_OK;
}

stark_err_t lab_pins_write(lab_pins_t *p, int pin, bool level)
{
    size_t i;
    stark_err_t gerr = guard(p, pin, &i);
    if (gerr != STARK_OK) {
        return gerr;
    }
    if (!p->hold[i].armed) {
        return STARK_ERR_STATE;
    }
    stark_err_t err = p->io.pad_out(p->io.ctx, pin, level);
    bool back = false;
    if (err == STARK_OK) {
        p->io.delay_us(p->io.ctx, LAB_CONTENTION_US);
        err = p->io.pad_read(p->io.ctx, pin, &back);
    }
    if (err != STARK_OK) {
        return fail(p, i, err);
    }
    if (back != level) {
        p->io.log(p->io.ctx, LAB_LOG_CONTENTION, pin, -1);
        return fail(p, i, STARK_ERR_IO);
    }
    return STARK_OK;
}

bool lab_pins_refresh(lab_pins_t *p)
{
    for (size_t i = 0; i < p->n; i++) {
        if (!offered_now(p, i)) {
            lose(p, i);
        } else if ((p->offered & (1u << i)) == 0u) {
            p->offered |= 1u << i; /* a slot became empty again */
            p->changed = true;
        }
    }
    return lab_pins_take_changed(p);
}

bool lab_pins_take_changed(lab_pins_t *p)
{
    bool c = p->changed;
    p->changed = false;
    return c;
}
