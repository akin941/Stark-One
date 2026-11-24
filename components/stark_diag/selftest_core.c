/*
 * selftest_core.c — the self-test runner (selftest_core.h). Pure; host-tested at 100 % of lines.
 */
#include "selftest_core.h"
#include <stdio.h>
#include <string.h>

static void lock(selftest_t *t)
{
    if (t->io.lock != NULL) {
        t->io.lock(t->io.ctx);
    }
}

static void unlock(selftest_t *t)
{
    if (t->io.unlock != NULL) {
        t->io.unlock(t->io.ctx);
    }
}

void selftest_init(selftest_t *t, const selftest_io_t *io)
{
    memset(t, 0, sizeof *t);
    t->io = *io;
    t->in_flight = SELFTEST_NONE;
}

stark_err_t selftest_register(selftest_t *t, const stark_selftest_check_t *table, size_t n)
{
    if (table == NULL || n == 0u || n > STARK_SELFTEST_MAX) {
        return STARK_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < n; i++) {
        if (table[i].name == NULL) {
            return STARK_ERR_INVALID_ARG;
        }
    }
    if (t->table != NULL) {
        return STARK_ERR_STATE;
    }
    lock(t);
    t->table = table;
    t->n = n;
    for (size_t i = 0; i < n; i++) {
        t->rows[i] = (stark_selftest_row_t){
            .name = table[i].name, .attested = table[i].attested, .result = ST_NOT_RUN};
    }
    unlock(t);
    return STARK_OK;
}

static bool known_result(stark_selftest_result_t r)
{
    return r == ST_NOT_RUN || r == ST_PASS || r == ST_FAIL || r == ST_SKIP || r == ST_ATTESTED;
}

/* Records one row: a short evidence line without control characters, then the notification. */
static void record(selftest_t *t, size_t index, stark_selftest_result_t r, const char *evidence,
                   uint32_t ms)
{
    stark_selftest_row_t copy;
    char summary[64];
    bool all_done = true;
    lock(t);
    stark_selftest_row_t *row = &t->rows[index];
    row->result = r;
    row->ms = ms;
    size_t i = 0;
    for (; evidence != NULL && evidence[i] != '\0' && i < STARK_SELFTEST_EVIDENCE; i++) {
        unsigned char c = (unsigned char)evidence[i];
        row->evidence[i] = (c < 0x20u || c == 0x7fu) ? '.' : evidence[i];
    }
    row->evidence[i] = '\0';
    if (t->in_flight == index) {
        t->in_flight = SELFTEST_NONE;
    }
    for (size_t k = 0; k < t->n; k++) {
        all_done = all_done && t->rows[k].result != ST_NOT_RUN;
    }
    copy = *row;
    if (all_done) {
        selftest_summary(t->rows, t->n, summary, sizeof summary);
    }
    unlock(t);
    if (t->io.notify != NULL && r != ST_NOT_RUN) { /* clearing a row is silent */
        t->io.notify(t->io.ctx, index, &copy, all_done ? summary : NULL);
    }
}

stark_err_t selftest_run(selftest_t *t, size_t index)
{
    if (t->table == NULL) {
        return STARK_ERR_STATE;
    }
    if (index >= t->n) {
        return STARK_ERR_INVALID_ARG;
    }
    const stark_selftest_check_t *chk = &t->table[index];
    if (chk->run == NULL) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (t->in_flight != SELFTEST_NONE) {
        size_t lost = t->in_flight; /* a check that never returned: the app was stopped inside it */
        t->in_flight = SELFTEST_NONE;
        record(t, lost, ST_FAIL, "app_fault", 0);
    }
    t->in_flight = index;
    char evidence[STARK_SELFTEST_EVIDENCE + 1u] = {0};
    uint32_t start = t->io.now_ms(t->io.ctx);
    stark_selftest_result_t r = chk->run(evidence, sizeof evidence);
    uint32_t ms = t->io.now_ms(t->io.ctx) - start;
    evidence[STARK_SELFTEST_EVIDENCE] = '\0';
    if (ms > SELFTEST_DEADLINE_MS) {
        r = ST_FAIL;
        (void)snprintf(evidence, sizeof evidence, "timeout");
    } else if (r != ST_PASS && r != ST_FAIL && r != ST_SKIP) {
        r = ST_FAIL;
        (void)snprintf(evidence, sizeof evidence, "bad result");
    }
    record(t, index, r, evidence, ms);
    return STARK_OK;
}

stark_err_t selftest_report_result(selftest_t *t, size_t index, stark_selftest_result_t r,
                                   const char *evidence)
{
    if (t->table == NULL) {
        return STARK_ERR_STATE;
    }
    if (index >= t->n || !known_result(r)) {
        return STARK_ERR_INVALID_ARG;
    }
    record(t, index, r, evidence, 0);
    return STARK_OK;
}

size_t selftest_results(selftest_t *t, stark_selftest_row_t *out, size_t max)
{
    lock(t);
    size_t n = t->n < max ? t->n : max;
    if (out != NULL && n > 0u) {
        memcpy(out, t->rows, n * sizeof out[0]);
    }
    size_t total = t->n;
    unlock(t);
    return total;
}

void selftest_summary(const stark_selftest_row_t *rows, size_t n, char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return;
    }
    unsigned count[5] = {0};
    for (size_t i = 0; i < n; i++) {
        if (known_result(rows[i].result)) {
            count[rows[i].result]++;
        }
    }
    int w = snprintf(buf, cap, "%u pass  %u attested  %u fail  %u skip", count[ST_PASS],
                     count[ST_ATTESTED], count[ST_FAIL], count[ST_SKIP]);
    if (count[ST_NOT_RUN] > 0u && w > 0 && (size_t)w < cap) {
        (void)snprintf(buf + w, cap - (size_t)w, "  %u not run", count[ST_NOT_RUN]);
    }
}

const char *selftest_result_name(stark_selftest_result_t r)
{
    switch (r) {
        case ST_PASS:
            return "pass";
        case ST_FAIL:
            return "fail";
        case ST_SKIP:
            return "skip";
        case ST_ATTESTED:
            return "attested";
        case ST_NOT_RUN:
        default:
            return "not run";
    }
}
