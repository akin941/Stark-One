/*
 * diag_core.c — see diag_core.h. Pure C.
 */
#include "diag_core.h"
#include <stdio.h>

uint32_t diag_fps_x10(uint32_t frames_now, uint32_t frames_prev, uint64_t now_us, uint64_t prev_us)
{
    uint64_t elapsed = now_us - prev_us;
    if (elapsed == 0) {
        return 0;
    }
    uint64_t frames = (uint32_t)(frames_now - frames_prev);
    uint64_t fps_x10 = (frames * 10000000u + elapsed / 2) / elapsed;
    return fps_x10 > UINT32_MAX ? UINT32_MAX : (uint32_t)fps_x10;
}

int diag_format(const stark_diag_snapshot_t *s, char *buf, size_t len)
{
    return snprintf(buf, len, "heap=%lu min=%lu fps=%lu.%lu drops=%lu overruns=%lu",
                    (unsigned long)s->heap_free, (unsigned long)s->heap_min,
                    (unsigned long)(s->fps_x10 / 10), (unsigned long)(s->fps_x10 % 10),
                    (unsigned long)s->ev_dropped, (unsigned long)s->ui.overruns);
}
