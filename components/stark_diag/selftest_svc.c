/*
 * selftest_svc.c — the self-test service (selftest_svc.h): the stark_selftest_*() calls of
 * stark_diag.h over one selftest_core instance, the log lines and the events.
 */
#include <stdbool.h>
#include "selftest_core.h"
#include "selftest_svc.h"
#include "stark_event.h"
#include "stark_log.h"

static selftest_t s_selftest;
static bool s_ready;
static stark_selftest_export_fn s_export;

/*
 * `selftest: <name> <result> <evidence>` (the name in lower case: "selftest: ram pass 32768 B"),
 * then a step event; when the last row was filled, `selftest: summary <counts>` and a done event.
 */
static void notify(void *ctx, size_t index, const stark_selftest_row_t *row, const char *summary)
{
    (void)ctx;
    (void)index;
    char name[24];
    size_t i = 0;
    for (; row->name[i] != '\0' && i < sizeof name - 1u; i++) {
        char c = row->name[i];
        name[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    name[i] = '\0';
    STARK_LOGI("selftest", "%s %s%s%s", name, selftest_result_name(row->result),
               row->evidence[0] != '\0' ? " " : "", row->evidence);
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_SELFTEST;
    e.system.arg = STARK_SELFTEST_EVT_STEP;
    (void)stark_event_publish(&e);
    if (summary != NULL) {
        STARK_LOGI("selftest", "summary %s", summary);
        e.system.arg = STARK_SELFTEST_EVT_DONE;
        (void)stark_event_publish(&e);
    }
}

void selftest_svc_init(uint32_t (*now_ms)(void *ctx), void (*lock)(void *ctx),
                       void (*unlock)(void *ctx))
{
    const selftest_io_t io = {.now_ms = now_ms, .lock = lock, .unlock = unlock, .notify = notify};
    selftest_init(&s_selftest, &io);
    s_export = NULL;
    s_ready = true;
}

stark_err_t stark_selftest_register(const stark_selftest_check_t *table, size_t n)
{
    return s_ready ? selftest_register(&s_selftest, table, n) : STARK_ERR_STATE;
}

stark_err_t stark_selftest_run(size_t index)
{
    return s_ready ? selftest_run(&s_selftest, index) : STARK_ERR_STATE;
}

void stark_selftest_report(size_t index, stark_selftest_result_t r, const char *evidence)
{
    if (s_ready) {
        (void)selftest_report_result(&s_selftest, index, r, evidence);
    }
}

size_t stark_selftest_results(stark_selftest_row_t *out, size_t max)
{
    return s_ready ? selftest_results(&s_selftest, out, max) : 0u;
}

void stark_selftest_set_export(stark_selftest_export_fn fn)
{
    s_export = fn;
}

stark_err_t stark_selftest_export(char *name, size_t cap, size_t *bytes)
{
    return s_export != NULL ? s_export(name, cap, bytes) : STARK_ERR_STATE;
}
