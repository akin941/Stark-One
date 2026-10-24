/*
 * ir_names.c — capture file names (ir_names.h). Pure.
 */
#include "ir_names.h"
#include <stdio.h>
#include <string.h>

void ir_names_init(ir_names_t *st)
{
    st->highest = 0;
}

bool ir_names_scan(ir_names_t *st, const char *name)
{
    if (name == NULL || strlen(name) != 10 || strncmp(name, "ir_", 3) != 0 ||
        strcmp(name + 7, ".ir") != 0) {
        return false;
    }
    uint32_t n = 0;
    for (int i = 3; i < 7; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        n = n * 10u + (uint32_t)(name[i] - '0');
    }
    if (n > st->highest) {
        st->highest = n;
    }
    return true;
}

stark_err_t ir_names_next(const ir_names_t *st, char *out, size_t len)
{
    if (out == NULL || len < 11) {
        return STARK_ERR_INVALID_ARG;
    }
    if (st->highest + 1u >= IR_NAME_MAX) {
        return STARK_ERR_NO_SPACE;
    }
    (void)snprintf(out, len, "ir_%04u.ir", (unsigned)(st->highest + 1u));
    return STARK_OK;
}

int ir_names_compare(const char *a, const char *b)
{
    ir_names_t probe;
    ir_names_init(&probe);
    bool ca = ir_names_scan(&probe, a);
    bool cb = ir_names_scan(&probe, b);
    if (ca != cb) {
        return ca ? -1 : 1;
    }
    return ca ? strcmp(b, a) : strcmp(a, b);
}
