/*
 * subghz_svc.c — the module service's decisions (subghz_svc.h). Pure.
 */
#include "subghz_svc.h"
#include <string.h>

void subghz_svc_init(subghz_svc_t *s, const subghz_svc_io_t *io)
{
    memset(s, 0, sizeof *s);
    s->io = *io;
    const cc1101_io_t drv_io = {io->xfer, io->now_ms, io->delay_ms, io->ctx};
    cc1101_bind(&s->drv, &drv_io);
}

bool subghz_svc_ready(const subghz_svc_t *s)
{
    return s->ready;
}

static void log_what(subghz_svc_t *s, subghz_log_t what, unsigned a, unsigned b)
{
    s->io.log(s->io.ctx, what, a, b);
}

/* The slot of a known module that declares a radio and both wires, or -1. */
static int find_slot(subghz_svc_t *s)
{
    stark_module_entry_t list[STARK_MODULE_ENTRIES];
    size_t n = s->io.list(s->io.ctx, list, STARK_MODULE_ENTRIES);
    if (n > STARK_MODULE_ENTRIES) {
        n = STARK_MODULE_ENTRIES;
    }
    for (size_t i = 0; i < n; i++) {
        if (list[i].kind == STARK_MODULE_SLOT && list[i].state == STARK_MODULE_KNOWN &&
            (list[i].declared & SUBGHZ_WIRING) == SUBGHZ_WIRING) {
            return list[i].slot;
        }
    }
    return -1;
}

static void backoff(subghz_svc_t *s, uint32_t ms)
{
    s->have_retry = true;
    s->retry_at = s->io.now_ms(s->io.ctx) + ms;
}

static void give_back(subghz_svc_t *s)
{
    s->io.close(s->io.ctx);
    s->io.release(s->io.ctx, &s->lease);
    s->claimed = false;
}

/* A rejection: try again later, and neither log a repeat of the last one nor try as often. */
static void note_failure(subghz_svc_t *s, subghz_log_t what, unsigned a, unsigned b)
{
    bool repeat = s->have_fail && s->fail_what == what && s->fail_a == a && s->fail_b == b;
    s->retry_ms = repeat ? s->retry_ms * 2u : SUBGHZ_RETRY_MS;
    if (s->retry_ms > SUBGHZ_RETRY_MAX_MS) {
        s->retry_ms = SUBGHZ_RETRY_MAX_MS;
    }
    backoff(s, s->retry_ms);
    if (!repeat) {
        s->have_fail = true;
        s->fail_what = what;
        s->fail_a = a;
        s->fail_b = b;
        log_what(s, what, a, b);
    }
}

/* The module was claimed but is not usable: let go of it and try again later. */
static void reject(subghz_svc_t *s, subghz_log_t what, unsigned a, unsigned b)
{
    give_back(s);
    note_failure(s, what, a, b);
}

static void leave_ready(subghz_svc_t *s, subghz_log_t what, unsigned a, unsigned b, bool wait)
{
    give_back(s);
    s->ready = false;
    s->misses = 0;
    s->have_fail = what == SUBGHZ_LOG_NOT_CC1101;
    s->fail_what = what;
    s->fail_a = a;
    s->fail_b = b;
    s->retry_ms = SUBGHZ_RETRY_MS;
    if (wait) {
        backoff(s, SUBGHZ_RETRY_MS);
    }
    log_what(s, what, a, b);
    s->io.changed(s->io.ctx, false);
}

/* A ready module is checked by its own answer every round. */
static void check_ready(subghz_svc_t *s)
{
    if (!s->io.lease_valid(s->io.ctx, &s->lease)) {
        leave_ready(s, SUBGHZ_LOG_LOST, 0, 0, false);
        return;
    }
    uint8_t part;
    uint8_t ver;
    stark_err_t err = cc1101_detect(&s->drv, &part, &ver);
    if (err == STARK_OK) {
        s->misses = 0;
    } else if (++s->misses >= SUBGHZ_MISS_LIMIT) {
        if (err == STARK_ERR_NOT_FOUND) {
            leave_ready(s, SUBGHZ_LOG_NOT_CC1101, part, ver, true);
        } else {
            leave_ready(s, SUBGHZ_LOG_LOST, 0, 0, true);
        }
    }
}

static void identify(subghz_svc_t *s, uint8_t slot)
{
    stark_err_t err = s->io.claim(s->io.ctx, slot, &s->lease);
    if (err != STARK_OK) {
        return; /* held by someone else, or the slot changed under us: try again next round */
    }
    s->claimed = true;
    s->none_logged = true; /* "no module" describes the start, not what happens later */
    err = s->io.open(s->io.ctx, &s->lease);
    if (err != STARK_OK) {
        s->io.release(s->io.ctx, &s->lease);
        s->claimed = false;
        note_failure(s, SUBGHZ_LOG_ERROR, (unsigned)err, 0);
        return;
    }
    stark_err_t reset_err = cc1101_reset(&s->drv);
    uint8_t part;
    uint8_t ver;
    err = cc1101_detect(&s->drv, &part, &ver); /* also when the reset failed: say what answered */
    if (err == STARK_ERR_NOT_FOUND) {
        reject(s, SUBGHZ_LOG_NOT_CC1101, part, ver);
        return;
    }
    if (err == STARK_OK) {
        err = reset_err;
    }
    if (err == STARK_OK) {
        err =
            cc1101_configure(&s->drv, CC1101_PRESET_OOK_RX, SUBGHZ_DEFAULT_HZ, SUBGHZ_DEFAULT_BAUD);
    }
    if (err != STARK_OK) {
        reject(s, SUBGHZ_LOG_ERROR, (unsigned)err, 0);
        return;
    }
    s->have_fail = false;
    s->misses = 0;
    s->ready = true;
    log_what(s, SUBGHZ_LOG_VERSION, ver, 0);
    s->io.changed(s->io.ctx, true);
}

void subghz_svc_step(subghz_svc_t *s)
{
    if (s->ready) {
        check_ready(s);
        return;
    }
    int slot = find_slot(s);
    if (slot < 0) {
        s->have_fail = false;
        if (!s->none_logged) {
            s->none_logged = true;
            log_what(s, SUBGHZ_LOG_NO_MODULE, 0, 0);
        }
        return;
    }
    if (s->have_retry && (int32_t)(s->io.now_ms(s->io.ctx) - s->retry_at) < 0) {
        return;
    }
    s->have_retry = false;
    identify(s, (uint8_t)slot);
}
