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

/* Stops the PWM if it is on table entry i: the channel stops and the transport releases the pad. */
static stark_err_t pwm_off(lab_pins_t *p, size_t i)
{
    if (p->pwm_idx != (int)i) {
        return STARK_OK;
    }
    p->pwm_idx = -1;
    p->hold[i].mode = STARK_LAB_MODE_HIZ;
    p->hold[i].hz = 0;
    p->hold[i].duty_pct = 0;
    return p->io.pwm_stop(p->io.ctx, p->tab[i].gpio);
}

/* The pin stops being offered: released to hi-Z if held, logged if it had been offered. */
static void lose(lab_pins_t *p, size_t i)
{
    if (p->hold[i].held) {
        (void)pwm_off(p, i);
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
    (void)pwm_off(p, i);
    (void)p->io.pad_hiz(p->io.ctx, p->tab[i].gpio);
    p->hold[i].armed = false;
    p->hold[i].mode = STARK_LAB_MODE_HIZ;
    return err;
}

/*
 * Drives the pad (the PWM stopped first), waits, and reads it back: a mismatch is a contention
 * and ends in hi-Z, as does any transport failure. On STARK_OK the pad is an output at `level`.
 */
static stark_err_t drive(lab_pins_t *p, size_t i, bool level)
{
    int pin = p->tab[i].gpio;
    stark_err_t err = pwm_off(p, i);
    bool back = false;
    if (err == STARK_OK) {
        err = p->io.pad_out(p->io.ctx, pin, level);
    }
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
    p->hold[i].mode = STARK_LAB_MODE_OUT;
    p->hold[i].level = level;
    return STARK_OK;
}

/* The pad as an input with the pull, the PWM stopped first; any failure ends in hi-Z. */
static stark_err_t set_input(lab_pins_t *p, size_t i, stark_lab_pull_t pull)
{
    stark_err_t err = pwm_off(p, i);
    if (err == STARK_OK) {
        err = p->io.pad_in(p->io.ctx, p->tab[i].gpio, (uint8_t)pull);
    }
    if (err != STARK_OK) {
        return fail(p, i, err);
    }
    p->hold[i].mode = STARK_LAB_MODE_IN;
    p->hold[i].pull = pull;
    return STARK_OK;
}

void lab_pins_init(lab_pins_t *p, const lab_io_t *io, const stark_board_pins_t *pins)
{
    memset(p, 0, sizeof(*p));
    p->pwm_idx = -1;
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
        (void)pwm_off(p, (size_t)i);
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
    return set_input(p, i, pull);
}

stark_err_t lab_pins_hiz(lab_pins_t *p, int pin)
{
    size_t i;
    stark_err_t gerr = guard(p, pin, &i);
    if (gerr != STARK_OK) {
        return gerr;
    }
    stark_err_t err = pwm_off(p, i);
    if (err == STARK_OK) {
        err = p->io.pad_hiz(p->io.ctx, pin);
    }
    if (err != STARK_OK) {
        return fail(p, i, err);
    }
    p->hold[i].mode = STARK_LAB_MODE_HIZ;
    p->hold[i].pull = STARK_LAB_PULL_NONE;
    return STARK_OK;
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
    stark_err_t err = pwm_off(p, i);
    if (err == STARK_OK) {
        err = p->io.pad_in(p->io.ctx, pin, STARK_LAB_PAD_UP);
    }
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
    p->hold[i].mode = STARK_LAB_MODE_IN;
    p->hold[i].pull = STARK_LAB_PULL_NONE;
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
    return drive(p, i, level);
}

stark_err_t lab_pins_toggle(lab_pins_t *p, int pin)
{
    size_t i;
    stark_err_t gerr = guard(p, pin, &i);
    if (gerr != STARK_OK) {
        return gerr;
    }
    if (!p->hold[i].armed || p->hold[i].mode != STARK_LAB_MODE_OUT) {
        return STARK_ERR_STATE;
    }
    return drive(p, i, !p->hold[i].level);
}

stark_err_t lab_pins_info(lab_pins_t *p, int pin, stark_lab_info_t *out)
{
    size_t i;
    stark_err_t gerr = guard(p, pin, &i);
    if (gerr != STARK_OK) {
        return gerr;
    }
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    *out = (stark_lab_info_t){.mode = p->hold[i].mode,
                              .pull = p->hold[i].pull,
                              .level = p->hold[i].level,
                              .hz = p->hold[i].hz,
                              .duty_pct = p->hold[i].duty_pct,
                              .armed = p->hold[i].armed};
    return STARK_OK;
}

stark_err_t lab_pins_pwm_start(lab_pins_t *p, int pin, uint32_t hz, uint32_t duty_pct)
{
    size_t i;
    stark_err_t gerr = guard(p, pin, &i);
    if (gerr != STARK_OK) {
        return gerr;
    }
    lab_pwm_plan_t plan;
    if (!lab_pwm_plan(hz, duty_pct, &plan)) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!p->hold[i].armed) {
        return STARK_ERR_STATE;
    }
    if (p->pwm_idx >= 0 && p->pwm_idx != (int)i) {
        return STARK_ERR_BUSY;
    }
    stark_err_t err = p->io.pwm_start(p->io.ctx, pin, hz, (uint8_t)duty_pct);
    if (err != STARK_OK) {
        return fail(p, i, err);
    }
    p->pwm_idx = (int)i;
    p->hold[i].mode = STARK_LAB_MODE_PWM;
    p->hold[i].hz = hz;
    p->hold[i].duty_pct = duty_pct;
    return STARK_OK;
}

void lab_pins_pwm_stop(lab_pins_t *p)
{
    if (p->pwm_idx >= 0) {
        size_t i = (size_t)p->pwm_idx;
        stark_err_t err = pwm_off(p, i);
        if (err != STARK_OK) {
            (void)fail(p, i, err);
        }
    }
}

stark_err_t lab_pins_pulse(lab_pins_t *p, int pin, bool level, uint32_t us)
{
    size_t i;
    stark_err_t gerr = guard(p, pin, &i);
    if (gerr != STARK_OK) {
        return gerr;
    }
    if (!lab_pulse_valid(us)) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!p->hold[i].armed) {
        return STARK_ERR_STATE;
    }
    /* What the pad was doing before, to go back to it; a PWM is stopped and gone, not restored. */
    stark_err_t err = pwm_off(p, i);
    if (err != STARK_OK) {
        return fail(p, i, err);
    }
    const stark_lab_mode_t was = p->hold[i].mode;
    const stark_lab_pull_t pull = p->hold[i].pull;
    const bool was_level = p->hold[i].level;
    /* The idle level first, read back: a pad that does not follow it is in contention. */
    err = drive(p, i, !level);
    if (err != STARK_OK) {
        return err;
    }
    err = p->io.pulse(p->io.ctx, pin, level, us);
    if (err != STARK_OK) {
        return fail(p, i, err);
    }
    if (was == STARK_LAB_MODE_OUT) {
        return drive(p, i, was_level);
    }
    if (was == STARK_LAB_MODE_IN) {
        return set_input(p, i, pull);
    }
    return lab_pins_hiz(p, pin);
}

/* One edge capture. A cancel (STARK_ERR_STATE) leaves the pin alone; a failure releases it. */
static stark_err_t capture(lab_pins_t *p, size_t i, uint32_t window_us, uint32_t *ts, size_t cap,
                           size_t *n, uint32_t *seen, bool *first)
{
    stark_err_t err = p->io.edges(p->io.ctx, p->tab[i].gpio, window_us, ts, cap, n, seen, first);
    if (err == STARK_OK || err == STARK_ERR_STATE) {
        return err;
    }
    return fail(p, i, err);
}

stark_err_t lab_pins_measure(lab_pins_t *p, int pin, uint32_t *ring, size_t cap, lab_meas_t *out)
{
    size_t i;
    stark_err_t gerr = guard(p, pin, &i);
    if (gerr != STARK_OK) {
        return gerr;
    }
    if (ring == NULL || out == NULL || cap < 2u) {
        return STARK_ERR_INVALID_ARG;
    }
    if (p->hold[i].mode != STARK_LAB_MODE_IN) {
        return STARK_ERR_STATE;
    }
    if (cap > STARK_LAB_MEAS_RING) {
        cap = STARK_LAB_MEAS_RING;
    }
    size_t n = 0;
    uint32_t seen = 0;
    bool first = false;
    /* The gate only counts: no ring, so it can say nothing but how many edges came. */
    stark_err_t err = capture(p, i, LAB_MEAS_GATE_MS * 1000u, NULL, 0, &n, &seen, &first);
    if (err != STARK_OK) {
        return err;
    }
    lab_meas_plan_t plan = lab_meas_plan(seen);
    if (plan.too_fast) {
        *out = (lab_meas_t){.status = STARK_LAB_MEAS_TOO_FAST, .edges = seen};
        return STARK_OK;
    }
    err = capture(p, i, plan.window_ms * 1000u, ring, cap, &n, &seen, &first);
    if (err != STARK_OK) {
        return err;
    }
    *out = lab_meas_compute(ring, n, first, p->io.tick_hz(p->io.ctx), seen > cap);
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
