/*
 * stark_subghz.h — L3 Sub-GHz service (TASKS.md STARK-0601…0605, ADR-0022): a
 * CC1101 in a module slot, reached through stark_subghz_transport. The pure
 * cores (cc1101_regs.c, cc1101_drv.c, subghz_svc.c) make every decision.
 *
 * Module state changes are published as STARK_EVT_SYSTEM / STARK_SYS_SUBGHZ
 * with system.arg = one of STARK_SUBGHZ_EVT_*. This version identifies the chip
 * and reports readiness; it neither receives nor transmits.
 */
#pragma once

#include <stdbool.h>
#include "stark_err.h"

#define STARK_SYS_SUBGHZ        7u

#define STARK_SUBGHZ_EVT_MODULE 1 /* stark_subghz_ready() changed */

/*
 * Starts the service task: it follows the module slots, claims one that declares
 * a radio, identifies the chip by its PARTNUM and VERSION, configures the 433 MHz
 * receive profile, and publishes STARK_SUBGHZ_EVT_MODULE on every change. Logs
 * `subghz: no module`, `subghz: cc1101 version 0x<v>`, `subghz: not a cc1101
 * (partnum 0x<p> version 0x<v>)` and `subghz: module lost`. Call after
 * stark_module_init(); never fails the boot for a missing module.
 */
stark_err_t stark_subghz_init(void);

/* True while a CC1101 is claimed, identified and answering. Any task. */
bool stark_subghz_ready(void);
