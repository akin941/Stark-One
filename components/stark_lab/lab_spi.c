/*
 * lab_spi.c — the SPI probe's rules (lab_spi.h). Pure and bounded: a request is a hash of at most
 * 32 bytes, a send one transaction of at most 32 bytes.
 */
#include "lab_spi.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "lab_i2c.h"
#include "stark_caps.h"
#include "stark_err.h"

void lab_spi_init(lab_spi_t *c, const lab_spi_io_t *io, const stark_board_pins_t *pins)
{
    memset(c, 0, sizeof(*c));
    c->io = *io;
    for (size_t i = 0; i < STARK_MODULE_SLOTS; i++) {
        c->cs[i] = pins->module[i].cs >= 0;
    }
}

static void set_why(stark_lab_spi_slot_t *out, const char *text)
{
    (void)snprintf(out->why, sizeof out->why, "%s", text);
}

/* The function a declared bit set belongs to: the one that owns the slot. */
static const char *owner_name(uint32_t declared)
{
    if ((declared & STARK_CAP_SUBGHZ) != 0u) {
        return "Sub-GHz";
    }
    if ((declared & STARK_CAP_NFC) != 0u) {
        return "NFC";
    }
    if ((declared & (STARK_CAP_IR_TX | STARK_CAP_IR_RX)) != 0u) {
        return "IR";
    }
    return "unknown";
}

void lab_spi_judge(const stark_module_entry_t *e, bool cs_wired, uint8_t slot,
                   stark_lab_spi_slot_t *out)
{
    memset(out, 0, sizeof(*out));
    out->slot = slot;
    if (e != NULL && e->state != STARK_MODULE_ABSENT) {
        (void)snprintf(out->name, sizeof out->name, "%s", e->name);
    }
    if (!cs_wired) {
        set_why(out, "no chip select");
    } else if (e == NULL || e->state == STARK_MODULE_ABSENT) {
        set_why(out, "no module");
    } else if (e->state == STARK_MODULE_GENERIC) {
        set_why(out, e->unsupported ? "unsupported module" : "generic module");
    } else if (e->state == STARK_MODULE_CONFLICT) {
        set_why(out, "conflict");
    } else if (stark_lab_spi_entry_ok(e)) {
        out->eligible = true;
    } else if ((e->declared & ~(uint32_t)(STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ)) != 0u) {
        (void)snprintf(out->why, sizeof out->why, "owned by %s", owner_name(e->declared));
    } else {
        set_why(out, "no SPI declared");
    }
}

/* The table's entry for `slot` (copied), or zeroes when it has none. True when it has one. */
static bool load(lab_spi_t *c, uint8_t slot, stark_module_entry_t *e)
{
    stark_module_entry_t list[STARK_MODULE_ENTRIES];
    size_t n = c->io.list(c->io.ctx, list, STARK_MODULE_ENTRIES);
    if (n > STARK_MODULE_ENTRIES) {
        n = STARK_MODULE_ENTRIES;
    }
    for (size_t i = 0; i < n; i++) {
        if (list[i].kind == STARK_MODULE_SLOT && list[i].slot == slot) {
            *e = list[i];
            return true;
        }
    }
    memset(e, 0, sizeof(*e));
    return false;
}

/* Judges `slot` as it is now; `e` is the table's entry for it (zeroes when it has none). */
static void judge_now(lab_spi_t *c, uint8_t slot, stark_module_entry_t *e,
                      stark_lab_spi_slot_t *out)
{
    bool found = load(c, slot, e);
    lab_spi_judge(found ? e : NULL, c->cs[slot], slot, out);
}

size_t lab_spi_slots(lab_spi_t *c, stark_lab_spi_slot_t *out, size_t max)
{
    size_t n = 0;
    for (uint8_t slot = 0; slot < STARK_MODULE_SLOTS && n < max; slot++) {
        stark_module_entry_t e;
        judge_now(c, slot, &e, &out[n++]);
    }
    return n;
}

static uint32_t fnv(uint32_t h, uint8_t b)
{
    return (h ^ b) * 16777619u;
}

static uint32_t fnv_le(uint32_t h, uint32_t v, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; i++) {
        h = fnv(h, (uint8_t)(v >> (8u * i)));
    }
    return h;
}

uint32_t lab_spi_fp(uint8_t slot, uint32_t hz, uint8_t mode, const uint8_t *tx, size_t n,
                    const stark_module_entry_t *e)
{
    uint32_t h = fnv(LAB_SPI_FP_SEED, (uint8_t)'S');
    h = fnv(h, slot);
    h = fnv_le(h, hz, 4u);
    h = fnv(h, mode);
    h = fnv(h, (uint8_t)n);
    for (size_t i = 0; tx != NULL && i < n; i++) {
        h = fnv(h, tx[i]);
    }
    h = fnv_le(h, e != NULL ? e->id : 0u, 2u);
    h = fnv(h, e != NULL ? e->rev : 0u);
    return fnv_le(h, e != NULL ? e->declared : 0u, 4u);
}

static void say(lab_spi_t *c, bool warn, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void say(lab_spi_t *c, bool warn, const char *fmt, ...)
{
    char text[LAB_SPI_LOG_MAX];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    c->io.log(c->io.ctx, warn, text);
}

static bool args_ok(uint8_t slot, uint32_t hz, uint8_t mode, size_t n)
{
    return slot < STARK_MODULE_SLOTS && stark_lab_spi_args_ok(hz, mode, n);
}

stark_err_t lab_spi_request(lab_spi_t *c, uint8_t slot, uint32_t hz, uint8_t mode,
                            const uint8_t *tx, size_t n, stark_lab_spi_req_t *out)
{
    c->armed = false; /* a new request replaces the old one, and a bad one leaves none */
    if (tx == NULL || out == NULL || !args_ok(slot, hz, mode, n)) {
        return STARK_ERR_INVALID_ARG;
    }
    stark_module_entry_t e;
    stark_lab_spi_slot_t s;
    judge_now(c, slot, &e, &s);
    if (!s.eligible) {
        say(c, true, "spi request refused: %s", s.why);
        return STARK_ERR_NOT_SUPPORTED;
    }
    memset(out, 0, sizeof(*out));
    out->slot = slot;
    out->hz = hz;
    out->mode = mode;
    out->n = (uint8_t)n;
    memcpy(out->tx, tx, n);
    c->fp = lab_spi_fp(slot, hz, mode, tx, n, &e);
    c->armed_at_ms = c->io.now_ms(c->io.ctx);
    c->armed = true;
    return STARK_OK;
}

/* The slot must be eligible and be the very transaction that was shown, module included. */
static stark_err_t recheck(lab_spi_t *c, const stark_lab_spi_req_t *req)
{
    stark_module_entry_t e;
    stark_lab_spi_slot_t s;
    judge_now(c, req->slot, &e, &s);
    if (!s.eligible) {
        say(c, true, "spi send refused: %s", s.why);
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (lab_spi_fp(req->slot, req->hz, req->mode, req->tx, req->n, &e) != c->fp) {
        say(c, true, "spi send refused: changed");
        return STARK_ERR_STATE;
    }
    return STARK_OK;
}

static void say_transaction(lab_spi_t *c, const stark_lab_spi_req_t *req, const uint8_t *rx,
                            stark_err_t err)
{
    char tx_hex[LAB_SPI_HEX_MAX];
    char rx_hex[LAB_SPI_HEX_MAX];
    (void)lab_i2c_hex(req->tx, req->n, tx_hex, sizeof tx_hex);
    if (err == STARK_OK) {
        (void)lab_i2c_hex(rx, req->n, rx_hex, sizeof rx_hex);
        say(c, false, "spi slot %u %s mode %u tx %s rx %s", (unsigned)req->slot,
            stark_lab_spi_clock_text(req->hz), (unsigned)req->mode, tx_hex, rx_hex);
    } else {
        say(c, true, "spi slot %u %s mode %u tx %s failed: %s", (unsigned)req->slot,
            stark_lab_spi_clock_text(req->hz), (unsigned)req->mode, tx_hex, stark_err_str(err));
    }
}

stark_err_t lab_spi_send(lab_spi_t *c, const stark_lab_spi_req_t *req, uint8_t *rx)
{
    bool armed = c->armed;
    uint32_t age = c->io.now_ms(c->io.ctx) - c->armed_at_ms; /* unsigned: survives the wrap */
    c->armed = false; /* consumed first, and so before the bus is touched */
    if (req == NULL || rx == NULL || !args_ok(req->slot, req->hz, req->mode, req->n)) {
        return STARK_ERR_INVALID_ARG;
    }
    memset(rx, 0, req->n);
    if (!armed || age > LAB_SPI_ARM_MS) {
        say(c, true, "spi send refused: not confirmed");
        return STARK_ERR_STATE;
    }
    stark_err_t err = recheck(c, req);
    if (err != STARK_OK) {
        return err;
    }
    stark_module_lease_t lease;
    err = c->io.claim(c->io.ctx, req->slot, &lease);
    if (err != STARK_OK) {
        say(c, true, "spi slot %u %s", (unsigned)req->slot,
            err == STARK_ERR_BUSY ? "busy" : stark_err_str(err));
        return err;
    }
    bool made = false;
    err = recheck(c, req); /* NOTE: a module cannot change under the lease; this closes the gap
                            * between the first check and the claim */
    if (err == STARK_OK) {
        made = true;
        err = c->io.xfer(c->io.ctx, &lease, req->hz, req->mode, req->tx, rx, req->n);
        say_transaction(c, req, rx, err);
    }
    c->io.release(c->io.ctx, &lease);
    if (made) {
        c->io.done(c->io.ctx);
    }
    return err;
}

void lab_spi_cancel(lab_spi_t *c)
{
    c->armed = false;
}

bool lab_spi_armed(const lab_spi_t *c)
{
    return c->armed;
}

/* The service's pure helpers (stark_lab.h), under the names the apps call. */
uint32_t stark_lab_spi_clock(size_t index)
{
    return stark_lab_spi_clock_hz(index);
}

const char *stark_lab_spi_clock_text(uint32_t hz)
{
    switch (hz) {
        case 100000u:
            return "100 kHz";
        case 250000u:
            return "250 kHz";
        case 500000u:
            return "500 kHz";
        case 1000000u:
            return "1 MHz";
        case 2000000u:
            return "2 MHz";
        case 4000000u:
            return "4 MHz";
        default:
            return "?";
    }
}
