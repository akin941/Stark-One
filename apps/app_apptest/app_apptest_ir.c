/*
 * app_apptest_ir.c — App Test's IR self-test (TASKS.md STARK-0304): after
 * a confirmation, the app worker turns RX on and transmits one fixed frame
 * of every protocol variant — the project's own lab test values, not any
 * device's codes — then checks that the same frame was received.
 *
 * On hardware this needs the IR LED to face the receiver. With TX wired
 * straight to RX instead (the validation setup of ADR-0019 §2) it proves
 * the firmware's encode -> RMT TX -> RMT RX -> decode path and nothing
 * optical.
 *
 * Logs `apptest: ir self-test <proto> ok|mismatch|timeout|refused: <err>`
 * per protocol, then `apptest: ir self-test done <ok>/<total>`.
 */
#include "apptest.h"
#include "ir_codec.h"
#include "stark_app.h"
#include "stark_hal.h"
#include "stark_ir.h"
#include "stark_log.h"
#include "ui_dialog.h"

typedef struct {
    ir_protocol_t protocol;
    uint16_t address, command;
} lab_frame_t;

static const lab_frame_t k_frames[] = {
    {IR_PROTO_NEC, 0x00, 0x12},     {IR_PROTO_NECX, 0x1234, 0x56}, {IR_PROTO_RC5, 0x05, 0x35},
    {IR_PROTO_RC6, 0x10, 0x20},     {IR_PROTO_SIRC12, 0x01, 0x15}, {IR_PROTO_SIRC15, 0x42, 0x2A},
    {IR_PROTO_SIRC20, 0x1A5, 0x33},
};

#define N_FRAMES (sizeof k_frames / sizeof k_frames[0])

static ir_signal_t s_tx, s_rx; /* worker only */

/* Transmit, retrying while the gate's interval runs; then wait for the echo. */
static const char *one(const lab_frame_t *f)
{
    s_tx = (ir_signal_t){
        .frame = {.protocol = f->protocol, .address = f->address, .command = f->command}};
    if (ir_encode(&s_tx.frame, &s_tx.timings, &s_tx.carrier_hz) != STARK_OK) {
        return "refused: encode";
    }
    while (stark_ir_take(&s_rx) == STARK_OK) {
    } /* nothing stale */
    stark_err_t err = STARK_ERR_BUSY;
    for (int i = 0; i < 40 && err == STARK_ERR_BUSY && !stark_app_worker_should_stop(); i++) {
        err = stark_ir_transmit(&s_tx, 0);
        if (err == STARK_ERR_BUSY) {
            stark_hal_delay_ms(50);
        }
    }
    if (err != STARK_OK) {
        return "refused";
    }
    for (int i = 0; i < 40 && !stark_app_worker_should_stop(); i++) {
        if (stark_ir_take(&s_rx) == STARK_OK) {
            return s_rx.frame.protocol == f->protocol && s_rx.frame.address == f->address &&
                           s_rx.frame.command == f->command
                       ? "ok"
                       : "mismatch";
        }
        stark_hal_delay_ms(20);
    }
    return "timeout";
}

static void self_test(void *ctx)
{
    (void)ctx;
    (void)stark_ir_rx_start();
    unsigned passed = 0;
    for (size_t i = 0; i < N_FRAMES && !stark_app_worker_should_stop(); i++) {
        const char *result = one(&k_frames[i]);
        passed += result[0] == 'o' ? 1u : 0u;
        STARK_LOGI("apptest", "ir self-test %s %s", ir_protocol_name(k_frames[i].protocol), result);
    }
    (void)stark_ir_rx_stop();
    STARK_LOGI("apptest", "ir self-test done %u/%u", passed, (unsigned)N_FRAMES);
}

static void confirmed(stark_dialog_result_t result, void *ctx)
{
    (void)ctx;
    if (result != STARK_DIALOG_OK) {
        STARK_LOGI("apptest", "ir self-test cancelled");
        return;
    }
    stark_err_t err = stark_app_worker_start(self_test, NULL);
    if (err != STARK_OK) {
        STARK_LOGW("apptest", "ir self-test not started: %s", stark_err_str(err));
    }
}

void apptest_ir_self_test(void)
{
    /* SECURITY_SCOPE §4.2: name what will be sent before sending it. */
    (void)stark_ui_dialog_confirm("Transmit IR test frames?",
                                  "NEC, NECX 38 kHz; RC5, RC6 36 kHz; SIRC 40 kHz. Own lab test "
                                  "values, one frame each.",
                                  confirmed, NULL);
}
