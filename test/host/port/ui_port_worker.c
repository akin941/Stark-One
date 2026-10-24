/*
 * ui_port_worker.c — host UI test port: the stark_app worker surface
 * without FreeRTOS. There is no worker task on the host: by default starting
 * one reports STARK_ERR_NOT_SUPPORTED and joining is a no-op, so the rest of
 * the app lifecycle (fault containment included) runs here unchanged; with
 * ui_port_worker_inline(true) the worker runs to completion inside the call,
 * its notifications reaching the app on the next tick. The real
 * worker (components/stark_app/app_worker.c) is exercised in the emulator
 * (test/emu/v01-app-lifecycle.toml).
 */
#include "app_internal.h"
#include "ui_port.h"

static bool s_inline;

void ui_port_worker_inline(bool on)
{
    s_inline = on;
}

stark_err_t stark_app_worker_start(stark_app_worker_fn fn, void *ctx)
{
    if (!s_inline || fn == NULL) {
        return fn == NULL ? STARK_ERR_INVALID_ARG : STARK_ERR_NOT_SUPPORTED;
    }
    fn(ctx);
    return STARK_OK;
}

bool stark_app_worker_should_stop(void)
{
    return false;
}

void app_worker_join(const char *app_id)
{
    (void)app_id;
}

uint32_t stark_app_worker_join_timeouts(void)
{
    return 0;
}
