/*
 * ui_port_uart.h — host UI test port: the lab service's UART group behind the UART app (TASKS.md
 * STARK-0705). It is stark_lab.h's stark_lab_uart_*() implemented over the real session
 * (components/stark_lab/lab_uart_svc.c), the real pin hold (lab_pins.c, the one the GPIO app uses),
 * the production transport guard (the host shim of lab_transport_uart.c) and the test-only UART
 * peer (test/fake/fake_lab.c), with the model clock advancing as reads and writes wait, so the app
 * is tested against the rules it ships with and what it leaves behind can be inspected (pins held,
 * drivers installed). It mirrors stark_lab.c: the same log lines, the same events. No lock: the
 * host has one thread. It also supplies stark_board_pins().
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stark_err.h"

/* Power-on: nothing open, no pin held, the peer silent, the clock at zero. */
void ui_port_uart_reset(void);

/* True while the UART tool (or anything) holds this lab pin. */
bool ui_port_uart_held(int pin);

/* Takes the pin for another owner, as the GPIO app would (STARK_ERR_BUSY when held); and gives it
 * back. */
stark_err_t ui_port_uart_take(int pin);
void ui_port_uart_give(int pin);

/* Drivers installed and removed since the reset: equal when nothing is left installed. */
unsigned ui_port_uart_opens(void);
unsigned ui_port_uart_closes(void);

/* A failing transport call, as lab_uart_host_fail(): 0 open, 1 set_baud, 2 read, 3 write. */
void ui_port_uart_fail(int call, int after);

/* STARK_LAB_EVT_UART_DATA and _ERROR published since the reset. */
unsigned ui_port_uart_data_events(void);
unsigned ui_port_uart_error_events(void);

/* The model clock, microseconds: where the peer's stream is "now". */
uint64_t ui_port_uart_time_us(void);
void ui_port_uart_advance_us(uint64_t us);
