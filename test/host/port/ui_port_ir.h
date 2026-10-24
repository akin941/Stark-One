/*
 * ui_port_ir.h — host UI test port: a stark_ir backend the test drives
 * (TASKS.md STARK-0304/0305): captures it queues, transmissions it records.
 */
#pragma once

#include <stdbool.h>
#include "stark_ir.h"

/* Queues a capture for stark_ir_take() and publishes STARK_SYS_IR / RX. */
void ui_port_ir_receive(const ir_signal_t *s);
/* What stark_ir_transmit() was last given; counts. */
const ir_signal_t *ui_port_ir_last_tx(void);
int ui_port_ir_tx_count(void);
bool ui_port_ir_rx_on(void);
/* The next stark_ir_transmit() returns err (STARK_OK: normal). */
void ui_port_ir_fail_next_tx(stark_err_t err);
