/*
 * module_bridge.c — drivers → stark_module → the launcher (module_bridge.h,
 * STARK-0502, STARK-0504). The event handler runs on the UI task: it is the
 * event bus's one consumer.
 */
#include "module_bridge.h"
#include "stark_app.h"
#include "stark_event.h"
#include "stark_ir.h"
#include "stark_module.h"
#include "stark_nfc.h"

static void confirm_nfc(void)
{
    stark_module_confirm(STARK_CAP_NFC, stark_nfc_module() == STARK_NFC_READY);
}

static void on_system(const stark_event_t *e, void *ctx)
{
    (void)ctx;
    if (e->system.id == STARK_SYS_NFC && e->system.arg == STARK_NFC_EVT_MODULE) {
        confirm_nfc();
    } else if (e->system.id == STARK_SYS_MODULE) {
        stark_app_set_caps(stark_module_caps());
    }
}

stark_err_t module_bridge_init(void)
{
    return stark_event_subscribe(STARK_EVT_MASK(STARK_EVT_SYSTEM), on_system, NULL);
}

void module_bridge_start(void)
{
    stark_module_provide(STARK_MODULE_ONBOARD, "IR", STARK_CAP_IR_TX | STARK_CAP_IR_RX);
    stark_module_provide(STARK_MODULE_BUS, "NFC reader", STARK_CAP_NFC);
    stark_module_confirm(STARK_CAP_IR_TX | STARK_CAP_IR_RX, stark_ir_fitted());
    confirm_nfc();
}
