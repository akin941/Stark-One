/*
 * selftest_target.h — the composition root's self-test wiring (TASKS.md STARK-0707): fills the
 * environment of selftest_env.h from the real services, registers the ten checks with
 * stark_diag and installs the report export. Target-only; the logic it wires is host-tested in
 * selftest_checks.c and selftest_export.c.
 */
#pragma once

#include "stark_err.h"

/* After stark_diag_init() (the runner is bound there) and the services the checks read.
 * Never boot-critical: without it the Self-Test app lists no checks. */
stark_err_t selftest_target_init(void);
