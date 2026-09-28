/*
 * ui_port_worker.c — host UI test port: the stark_app worker surface
 * without FreeRTOS. There is no worker on the host: starting one reports
 * STARK_ERR_NOT_SUPPORTED and joining is a no-op, so the rest of the app
 * lifecycle (fault containment included) runs here unchanged. The real
 * worker (components/stark_app/app_worker.c) is exercised in the emulator
 * (test/emu/v01-app-lifecycle.toml).
 */
#include "app_internal.h"

stark_err_t stark_app_worker_start(stark_app_worker_fn fn, void *ctx)
{
    (void)fn;
    (void)ctx;
    return STARK_ERR_NOT_SUPPORTED;
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
