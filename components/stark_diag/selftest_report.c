/*
 * selftest_report.c — the self-test report formatter (selftest_report.h). Pure; host-tested at
 * 100 % of lines.
 */
#include "selftest_report.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "selftest_core.h"

#define LINE_MAX_CHARS 191u /* one formatted line, before its newline */
#define TAIL_CHUNK     128u

static const char k_truncated[] = "-- truncated --\n";
#define RESERVE (sizeof k_truncated - 1u) /* room kept for the marker: it always fits */

typedef struct {
    selftest_sink_fn sink;
    void *ctx;
    size_t cap;
    size_t used;
    bool truncated;
    stark_err_t err;
} writer_t;

/* Hands n bytes to the sink unless the cap would be passed (the marker keeps its reserve). */
static void emit(writer_t *w, const char *s, size_t n)
{
    if (w->err != STARK_OK || w->truncated || n == 0u) {
        return;
    }
    if (w->used + n + RESERVE > w->cap) {
        w->truncated = true;
        return;
    }
    w->err = w->sink(w->ctx, s, n);
    w->used += n;
}

static bool is_control(char ch)
{
    unsigned char c = (unsigned char)ch;
    return c < 0x20u || c == 0x7fu;
}

/* One line: formatted, every control character a '.', then a newline. */
static void line(writer_t *w, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void line(writer_t *w, const char *fmt, ...)
{
    char buf[LINE_MAX_CHARS + 2u];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, LINE_MAX_CHARS + 1u, fmt, ap);
    va_end(ap);
    size_t n = strlen(buf);
    for (size_t i = 0; i < n; i++) {
        if (is_control(buf[i])) {
            buf[i] = '.';
        }
    }
    buf[n++] = '\n';
    emit(w, buf, n);
}

static const char *text(const char *s)
{
    return s != NULL ? s : "(none)";
}

static void section_snapshot(writer_t *w, const stark_diag_snapshot_t *s)
{
    if (s == NULL) {
        line(w, "diag: n/a");
        return;
    }
    line(w,
         "diag: heap=%lu min=%lu largest=%lu fps=%lu.%lu events=%lu drops=%lu peak=%lu "
         "overruns=%lu render_err=%lu join_to=%lu",
         (unsigned long)s->heap_free, (unsigned long)s->heap_min, (unsigned long)s->heap_largest,
         (unsigned long)(s->fps_x10 / 10u), (unsigned long)(s->fps_x10 % 10u),
         (unsigned long)s->ev_published, (unsigned long)s->ev_dropped,
         (unsigned long)s->ev_max_depth, (unsigned long)s->ui.overruns,
         (unsigned long)s->ui.render_errors, (unsigned long)s->ui.join_timeouts);
}

static void section_tail(writer_t *w, const selftest_report_src_t *src, size_t tail_max)
{
    line(w, "log tail:");
    if (src->tail == NULL) {
        return;
    }
    char chunk[TAIL_CHUNK];
    char last = '\n';
    size_t offset = 0;
    while (offset < tail_max && w->err == STARK_OK && !w->truncated) {
        size_t want = tail_max - offset < TAIL_CHUNK ? tail_max - offset : TAIL_CHUNK;
        size_t got = src->tail(src->tail_ctx, offset, chunk, want);
        if (got == 0u) {
            break;
        }
        if (got > want) {
            got = want; /* a provider that over-delivers is held to what was asked */
        }
        for (size_t i = 0; i < got; i++) {
            if (chunk[i] != '\n' && is_control(chunk[i])) {
                chunk[i] = '.';
            }
        }
        emit(w, chunk, got);
        last = chunk[got - 1u];
        offset += got;
    }
    if (last != '\n') {
        emit(w, "\n", 1u);
    }
}

stark_err_t selftest_report_write(const selftest_report_src_t *src, size_t cap, size_t tail_max,
                                  selftest_sink_fn sink, void *sink_ctx, size_t *written,
                                  bool *truncated)
{
    if (src == NULL || sink == NULL || cap < 64u) {
        return STARK_ERR_INVALID_ARG;
    }
    writer_t w = {.sink = sink, .ctx = sink_ctx, .cap = cap};
    line(&w, "STARK ONE self-test report");
    line(&w, "firmware: %s", text(src->version));
    line(&w, "build: %s", text(src->build));
    line(&w, "board: %s", text(src->board));
    line(&w, "uptime: %lu s", (unsigned long)src->uptime_s);
    section_snapshot(&w, src->snap);
    line(&w, "tasks: %lu", (unsigned long)src->ntasks);
    for (size_t i = 0; src->tasks != NULL && i < src->ntasks; i++) {
        line(&w, "  %.15s stack free %lu", src->tasks[i].name,
             (unsigned long)src->tasks[i].stack_free);
    }
    line(&w, "modules: %lu", (unsigned long)src->nmodules);
    for (size_t i = 0; src->modules != NULL && i < src->nmodules; i++) {
        line(&w, "  %s", text(src->modules[i]));
    }
    line(&w, "storage: %s", text(src->storage));
    line(&w, "self-test: %lu checks", (unsigned long)src->nrows);
    for (size_t i = 0; src->rows != NULL && i < src->nrows; i++) {
        const stark_selftest_row_t *r = &src->rows[i];
        line(&w, "  %s: %s%s%s%s", text(r->name), selftest_result_name(r->result),
             r->attested ? " (operator)" : "", r->evidence[0] != '\0' ? " " : "", r->evidence);
    }
    if (src->rows != NULL) {
        char sum[64];
        selftest_summary(src->rows, src->nrows, sum, sizeof sum);
        line(&w, "summary: %s", sum);
    }
    section_tail(&w, src, tail_max);
    line(&w, "-- end --");
    if (w.truncated && w.err == STARK_OK) {
        w.err = sink(sink_ctx, k_truncated, sizeof k_truncated - 1u);
        w.used += sizeof k_truncated - 1u;
    }
    if (written != NULL) {
        *written = w.used;
    }
    if (truncated != NULL) {
        *truncated = w.truncated;
    }
    return w.err;
}

bool selftest_report_name(unsigned number, char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (number > 9999u || cap < SELFTEST_REPORT_NAME_MAX) {
        return false;
    }
    (void)snprintf(buf, cap, "report_%04u.txt", number);
    return true;
}

bool selftest_report_parse(const char *name, unsigned *number)
{
    if (name == NULL || strlen(name) != 15u || strncmp(name, "report_", 7u) != 0 ||
        strcmp(name + 11, ".txt") != 0) {
        return false;
    }
    unsigned v = 0;
    for (size_t i = 7; i < 11u; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        v = v * 10u + (unsigned)(name[i] - '0');
    }
    if (number != NULL) {
        *number = v;
    }
    return true;
}
