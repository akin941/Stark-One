/*
 * ui_port_diag.c — host UI test port: a stark_diag backend whose snapshot
 * and task table the test sets (ui_port_diag.h), so the real Diagnostics
 * screen can be rendered on the host with known values.
 */
#include <string.h>
#include "stark_diag.h"
#include "ui_port_diag.h"

static stark_diag_snapshot_t s_snap;
static stark_diag_task_t s_tasks[4];
static size_t s_task_count;

void ui_port_diag_set(const stark_diag_snapshot_t *snap, const stark_diag_task_t *tasks, size_t n)
{
    s_snap = *snap;
    s_task_count = n < 4 ? n : 4;
    memcpy(s_tasks, tasks, s_task_count * sizeof tasks[0]);
}

stark_err_t stark_diag_init(stark_diag_ui_fn ui_source)
{
    (void)ui_source;
    return STARK_OK;
}

stark_err_t stark_diag_snapshot(stark_diag_snapshot_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    *out = s_snap;
    return STARK_OK;
}

size_t stark_diag_tasks(stark_diag_task_t *out, size_t max)
{
    size_t n = s_task_count < max ? s_task_count : max;
    memcpy(out, s_tasks, n * sizeof s_tasks[0]);
    return n;
}
