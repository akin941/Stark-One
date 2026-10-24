/*
 * ui_port_ir.c — host UI test port: settable stark_ir backend
 * (ui_port_ir.h). Transmissions complete at once (TX_DONE published).
 */
#include "stark_event.h"
#include "ui_port_ir.h"

#define SLOTS 8

static ir_signal_t s_queue[SLOTS];
static int s_head, s_count;
static ir_signal_t s_last_tx;
static int s_tx;
static bool s_rx_on;
static stark_err_t s_fail;

static void publish(int32_t what)
{
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_IR;
    e.system.arg = what;
    (void)stark_event_publish(&e);
}

void ui_port_ir_receive(const ir_signal_t *s)
{
    if (s_count < SLOTS) {
        s_queue[(s_head + s_count++) % SLOTS] = *s;
    }
    publish(STARK_IR_EVT_RX);
}

const ir_signal_t *ui_port_ir_last_tx(void)
{
    return &s_last_tx;
}

int ui_port_ir_tx_count(void)
{
    return s_tx;
}

bool ui_port_ir_rx_on(void)
{
    return s_rx_on;
}

void ui_port_ir_fail_next_tx(stark_err_t err)
{
    s_fail = err;
}

stark_err_t stark_ir_init(void)
{
    return STARK_OK;
}

stark_err_t stark_ir_rx_start(void)
{
    s_rx_on = true;
    return STARK_OK;
}

stark_err_t stark_ir_rx_stop(void)
{
    s_rx_on = false;
    return STARK_OK;
}

stark_err_t stark_ir_take(ir_signal_t *out)
{
    if (s_count == 0) {
        return STARK_ERR_NOT_FOUND;
    }
    *out = s_queue[s_head];
    s_head = (s_head + 1) % SLOTS;
    s_count--;
    return STARK_OK;
}

stark_err_t stark_ir_transmit(const ir_signal_t *s, uint8_t repeats)
{
    (void)repeats;
    if (s_fail != STARK_OK) {
        stark_err_t err = s_fail;
        s_fail = STARK_OK;
        return err;
    }
    s_last_tx = *s;
    s_tx++;
    publish(STARK_IR_EVT_TX_DONE);
    return STARK_OK;
}

stark_err_t stark_ir_stats(stark_ir_stats_t *out)
{
    *out = (stark_ir_stats_t){0};
    return STARK_OK;
}
