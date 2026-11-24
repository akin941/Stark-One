/*
 * lab_transport_uart_host.h — test-only control of the host shim of the lab transport's UART group
 * (lab_transport_uart_host.c): the model clock the fake peer runs on and the faults to inject.
 * Only test files and the host UI port include it; the firmware never sees this directory.
 */
#pragma once

#include <stdint.h>

/* Everything back to the start: the clock at zero, no fault, the driver removed. */
void lab_uart_host_reset(void);

/* The model clock in microseconds (the fake peer's, and what the session's now_ms reads). It moves
 * when a read waits out its timeout or a write waits out its wire time, and when a test sets it. */
uint64_t lab_uart_host_time_us(void);
void lab_uart_host_set_time_us(uint64_t us);

/* Faults: the transport call fails with STARK_ERR_IO (a read or a write: a negative result) after
 * `after` more successful calls of its kind (0: the next one), until cleared with after < 0. */
typedef enum {
    LAB_UART_HOST_OPEN,
    LAB_UART_HOST_SET_BAUD,
    LAB_UART_HOST_READ,
    LAB_UART_HOST_WRITE,
    LAB_UART_HOST_KINDS
} lab_uart_host_call_t;
void lab_uart_host_fail(lab_uart_host_call_t call, int after);

/* True while a driver is installed (what the session promises to remove on every exit). */
unsigned lab_uart_host_opens(void);  /* installs since the reset */
unsigned lab_uart_host_closes(void); /* removals since the reset */
