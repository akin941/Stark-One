/*
 * app_apptest_worker.c — App Test's worker and fault items (TASKS.md
 * STARK-0108): a worker that reports progress and stops when asked, a fault
 * raised on the UI task, one raised by the worker, and a worker that ignores
 * the stop request (the join timeout path).
 */
#include "apptest.h"
#include "stark_app.h"
#include "stark_event.h"
#include "stark_hal.h"
#include "stark_log.h"

static void demo(void *ctx)
{
    (void)ctx;
    for (int32_t n = 1; !stark_app_worker_should_stop(); n++) {
        stark_event_t e = {.type = STARK_EVT_APP_REQUEST};
        e.app.id = STARK_APP_REQ_NOTIFY;
        e.app.arg = n;
        (void)stark_event_publish(&e);
        stark_hal_delay_ms(200);
    }
}

static void fail_here(void *ctx)
{
    (void)ctx;
    stark_app_fail(STARK_ERR_TIMEOUT);
}

static void ignore_stop(void *ctx)
{
    (void)ctx;
    for (;;) {
        stark_hal_delay_ms(50); /* never polls stark_app_worker_should_stop() */
    }
}

static void start(stark_app_worker_fn fn, const char *what)
{
    stark_err_t err = stark_app_worker_start(fn, NULL);
    if (err == STARK_OK) {
        STARK_LOGI("apptest", "worker started: %s", what);
    } else {
        STARK_LOGW("apptest", "worker %s not started: %s", what, stark_err_str(err));
    }
}

void apptest_run(size_t which)
{
    switch (which) {
        case 0:
            start(demo, "demo");
            break;
        case 1:
            stark_app_fail(STARK_ERR_IO);
            break;
        case 2:
            start(fail_here, "fail");
            break;
        default:
            start(ignore_stop, "ignore-stop");
            break;
    }
}
