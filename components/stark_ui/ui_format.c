/*
 * ui_format.c — see ui_format.h. Pure C.
 */
#include "ui_format.h"
#include <inttypes.h>
#include <stdio.h>

size_t ui_format_uptime(uint64_t seconds, char *buf, size_t size)
{
    uint64_t days = seconds / 86400u;
    unsigned h = (unsigned)(seconds / 3600u % 24u);
    unsigned m = (unsigned)(seconds / 60u % 60u);
    unsigned s = (unsigned)(seconds % 60u);
    int n = days > 0 ? snprintf(buf, size, "%" PRIu64 "d %02u:%02u:%02u", days, h, m, s)
                     : snprintf(buf, size, "%02u:%02u:%02u", h, m, s);
    return n > 0 ? (size_t)n : 0;
}
