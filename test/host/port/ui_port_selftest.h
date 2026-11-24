/*
 * ui_port_selftest.h — host UI test port: the self-test service behind the Self-Test app (TASKS.md
 * STARK-0707). The service is the production selftest_core.c and selftest_svc.c (the real log
 * lines and events) over the fake clock; what the checks themselves return is the test's, through
 * a table of six fake automated checks in the production order. The four the app observes have no
 * `run`, as in production.
 */
#pragma once

#include <stddef.h>
#include "stark_diag.h"

/* A fresh, empty service on the fake clock, and the default outcomes (all pass, Storage skipped
 * for want of a card: the production emulator's own result). Nothing is registered until
 * ui_port_selftest_register(). */
void ui_port_selftest_reset(void);

/* Registers the ten-check table (the production names, order and kinds); `n` of them (the first n
 * entries) so a short table can be tried. */
stark_err_t ui_port_selftest_register(size_t n);

/* What fake automated check `index` (0..5) returns next time it runs. */
void ui_port_selftest_outcome(size_t index, stark_selftest_result_t r, const char *evidence);

/* How many times each fake check was run, in total. */
unsigned ui_port_selftest_runs(size_t index);
