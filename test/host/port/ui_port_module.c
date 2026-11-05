/*
 * ui_port_module.c — the host UI port's fake stark_module (ui_port_module.h).
 */
#include "ui_port_module.h"
#include <string.h>

static stark_module_entry_t s_list[STARK_MODULE_ENTRIES];
static size_t s_n;

void ui_port_module_reset(void)
{
    s_n = 0;
}

void ui_port_module_set(const stark_module_entry_t *list, size_t n)
{
    memcpy(s_list, list, n * sizeof *list);
    s_n = n;
}

size_t stark_module_list(stark_module_entry_t *out, size_t max)
{
    for (size_t i = 0; i < s_n && i < max; i++) {
        out[i] = s_list[i];
    }
    return s_n;
}
