/*
 * subghz_svc.h — the module service's decisions (TASKS.md STARK-0601,
 * ADR-0022 §2): find a slot that declares a radio, claim it, identify the chip
 * by its own answer, configure the 433 MHz profile, and keep checking. Pure,
 * over an injected I/O table; stark_subghz.c binds it to stark_module,
 * stark_subghz_transport and FreeRTOS, and the host tests bind it to the real
 * module core and the fake CC1101.
 *
 * A descriptor only declares. The service reports READY only after
 * cc1101_detect() has succeeded, and the composition root turns that into the
 * capability (main/module_bridge.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cc1101_drv.h"
#include "stark_module.h"

#define SUBGHZ_WIRING       (STARK_CAP_SUBGHZ | STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ)
#define SUBGHZ_DEFAULT_HZ   433920000u
#define SUBGHZ_DEFAULT_BAUD 4800u
#define SUBGHZ_RETRY_MS     2000u /* before a rejected module is identified again */
#define SUBGHZ_RETRY_MAX_MS 8000u /* ... doubling while the same rejection repeats, up to this */
#define SUBGHZ_MISS_LIMIT   2u    /* failed checks in a row that drop a ready module */

typedef enum {
    SUBGHZ_LOG_NO_MODULE,  /* nothing declares a radio */
    SUBGHZ_LOG_VERSION,    /* a = VERSION */
    SUBGHZ_LOG_NOT_CC1101, /* a = PARTNUM, b = VERSION */
    SUBGHZ_LOG_LOST,       /* a ready module went away */
    SUBGHZ_LOG_ERROR       /* a = stark_err_t of the step that failed */
} subghz_log_t;

typedef struct {
    size_t (*list)(void *ctx, stark_module_entry_t *out, size_t max);
    stark_err_t (*claim)(void *ctx, uint8_t slot, stark_module_lease_t *out);
    void (*release)(void *ctx, const stark_module_lease_t *lease);
    bool (*lease_valid)(void *ctx, const stark_module_lease_t *lease);
    stark_err_t (*open)(void *ctx, const stark_module_lease_t *lease);
    void (*close)(void *ctx);
    stark_err_t (*xfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);
    uint32_t (*now_ms)(void *ctx);
    void (*delay_ms)(void *ctx, uint32_t ms);
    void (*log)(void *ctx, subghz_log_t what, unsigned a, unsigned b);
    void (*changed)(void *ctx, bool ready); /* after every change of readiness */
    void *ctx;
} subghz_svc_io_t;

typedef struct {
    subghz_svc_io_t io;
    cc1101_t drv;
    stark_module_lease_t lease;
    bool claimed;     /* lease and transport are held */
    bool ready;       /* identified, configured, confirmed to the world */
    bool none_logged; /* "no module" is said once */
    bool have_retry;  /* retry_at is set */
    uint32_t retry_at;
    uint32_t retry_ms; /* the current wait: it doubles while the same rejection repeats */
    bool have_fail;    /* the last rejection, so a repeat is not logged again */
    subghz_log_t fail_what;
    unsigned fail_a;
    unsigned fail_b;
    uint8_t misses;
} subghz_svc_t;

void subghz_svc_init(subghz_svc_t *s, const subghz_svc_io_t *io);

/* One round, bounded: at most one claim, one identification and one configuration. */
void subghz_svc_step(subghz_svc_t *s);

bool subghz_svc_ready(const subghz_svc_t *s);
