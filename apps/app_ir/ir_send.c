/*
 * ir_send.c — the IR app's transmit step (TASKS.md STARK-0306): the signal
 * Browse loaded into irapp_work is checked against the transmit gate, shown
 * in a confirmation that says exactly what will be sent (SECURITY_SCOPE
 * §4.2), and transmitted only on OK; the result is logged by file name.
 */
#include <stdio.h>
#include "ir_tx_guard.h"
#include "irapp.h"
#include "stark_ir.h"
#include "stark_log.h"
#include "ui_dialog.h"

static char s_name[24]; /* the file being sent, for the log */
static bool s_sending;

static void send_confirmed(stark_dialog_result_t result, void *ctx)
{
    (void)ctx;
    if (result != STARK_DIALOG_OK) {
        STARK_LOGI("irapp", "send cancelled");
        return;
    }
    stark_err_t err = stark_ir_transmit(&irapp_work.signal, 0);
    if (err == STARK_OK) {
        s_sending = true;
    } else if (err == STARK_ERR_BUSY) {
        STARK_LOGI("irapp", "send refused: busy");
        (void)stark_ui_dialog_alert("Wait a moment", "One transmission per second.", NULL, NULL);
    } else {
        STARK_LOGW("irapp", "send refused: %s", stark_err_str(err));
    }
}

void irsend_confirm(const char *name)
{
    ir_tx_plan_t plan;
    char what[24];
    char msg[112];
    snprintf(s_name, sizeof s_name, "%s", name);
    if (ir_tx_check(&irapp_work.signal, 0, &plan) != STARK_OK) {
        STARK_LOGW("irapp", "send refused: %s outside the transmit limits", s_name);
        return;
    }
    irapp_describe(&irapp_work.signal, what, sizeof what);
    snprintf(msg, sizeof msg, "%s, %u Hz, %u pulses, %u ms. Own devices only.", what,
             (unsigned)irapp_work.signal.carrier_hz, (unsigned)irapp_work.signal.timings.n,
             (unsigned)plan.total_ms);
    STARK_LOGI("irapp", "confirm %s: %s", s_name, msg);
    (void)stark_ui_dialog_confirm("Transmit IR?", msg, send_confirmed, NULL);
}

void irsend_event(const stark_event_t *e)
{
    if (e->type == STARK_EVT_SYSTEM && e->system.id == STARK_SYS_IR && s_sending &&
        (e->system.arg == STARK_IR_EVT_TX_DONE || e->system.arg == STARK_IR_EVT_TX_FAILED)) {
        s_sending = false;
        if (e->system.arg == STARK_IR_EVT_TX_DONE) {
            STARK_LOGI("irapp", "sent %s", s_name);
        } else {
            STARK_LOGW("irapp", "send failed: %s", s_name);
        }
    }
}

void irsend_stop(void)
{
    s_sending = false;
}
