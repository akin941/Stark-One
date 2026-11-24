/*
 * selftest_svc.h — private to stark_diag: the one self-test the firmware has (TASKS.md STARK-0707).
 * selftest_svc.c implements the stark_selftest_*() calls of stark_diag.h over a single
 * selftest_core instance, logs every result and publishes the step and done events. It needs only
 * the log and the event bus, both of which the host has, so the host UI port runs this very file;
 * what is target-only is the clock and the mutex that stark_diag.c hands in.
 */
#pragma once

#include <stdint.h>

/* Starts a fresh, empty self-test: no checks registered, no exporter installed. `lock` and
 * `unlock` guard the result table against a reader on another task (NULL on the host, which has
 * one thread). */
void selftest_svc_init(uint32_t (*now_ms)(void *ctx), void (*lock)(void *ctx),
                       void (*unlock)(void *ctx));
