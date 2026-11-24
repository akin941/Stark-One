/*
 * ui_port_selftest.c — see ui_port_selftest.h. Test-only.
 */
#include "ui_port_selftest.h"
#include <stdio.h>
#include <string.h>
#include "selftest_svc.h"
#include "stark_hal.h"

#define FAKES 6u

static stark_selftest_result_t s_result[FAKES];
static char s_evidence[FAKES][STARK_SELFTEST_EVIDENCE + 1u];
static unsigned s_runs[FAKES];

static uint32_t now_ms(void *ctx)
{
    (void)ctx;
    return (uint32_t)(stark_hal_now_us() / 1000u);
}

static stark_selftest_result_t fake(size_t i, char *evidence, size_t cap)
{
    s_runs[i]++;
    (void)snprintf(evidence, cap, "%s", s_evidence[i]);
    return s_result[i];
}

static stark_selftest_result_t fake0(char *e, size_t c)
{
    return fake(0, e, c);
}
static stark_selftest_result_t fake1(char *e, size_t c)
{
    return fake(1, e, c);
}
static stark_selftest_result_t fake2(char *e, size_t c)
{
    return fake(2, e, c);
}
static stark_selftest_result_t fake3(char *e, size_t c)
{
    return fake(3, e, c);
}
static stark_selftest_result_t fake4(char *e, size_t c)
{
    return fake(4, e, c);
}
static stark_selftest_result_t fake5(char *e, size_t c)
{
    return fake(5, e, c);
}

static const stark_selftest_check_t k_table[10] = {
    {"RAM", false, fake0},      {"Flash", false, fake1},      {"Settings", false, fake2},
    {"Storage", false, fake3},  {"Module bus", false, fake4}, {"Events and stacks", false, fake5},
    {"Buttons", false, NULL},   {"Display", true, NULL},      {"Buzzer", true, NULL},
    {"Status LED", true, NULL},
};

void ui_port_selftest_outcome(size_t index, stark_selftest_result_t r, const char *evidence)
{
    s_result[index] = r;
    (void)snprintf(s_evidence[index], sizeof s_evidence[index], "%s", evidence);
}

void ui_port_selftest_reset(void)
{
    selftest_svc_init(now_ms, NULL, NULL);
    memset(s_runs, 0, sizeof s_runs);
    ui_port_selftest_outcome(0, ST_PASS, "32768 B");
    ui_port_selftest_outcome(1, ST_PASS, "4 MB, image 556784 B, sha 1a2b3c4d");
    ui_port_selftest_outcome(2, ST_PASS, "4 settings valid, from nvs");
    ui_port_selftest_outcome(3, ST_SKIP, "no card");
    ui_port_selftest_outcome(4, ST_PASS, "table agrees with probes, 2 slots");
    ui_port_selftest_outcome(5, ST_PASS, "drops 0, 9 tasks, min stack 612 B");
}

stark_err_t ui_port_selftest_register(size_t n)
{
    return stark_selftest_register(k_table, n);
}

unsigned ui_port_selftest_runs(size_t index)
{
    return s_runs[index];
}
