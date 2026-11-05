/*
 * module_bridge.c — module state → the launcher (module_bridge.h). Runs on the
 * UI task: it is the event bus's one consumer.
 */
#include "module_bridge.h"
#include "stark_app.h"
#include "stark_event.h"
#include "stark_module.h"

static void on_system(const stark_event_t *e, void *ctx)
{
    (void)ctx;
    if (e->system.id == STARK_SYS_MODULE) {
        stark_app_set_caps(stark_module_caps());
    }
}

stark_err_t module_bridge_init(void)
{
    return stark_event_subscribe(STARK_EVT_MASK(STARK_EVT_SYSTEM), on_system, NULL);
}
