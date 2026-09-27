/*
 * ppm_dump.c — see ppm_dump.h. Expands RGB565 to 8-bit channels.
 */
#include "ppm_dump.h"
#include <stdio.h>

static uint8_t expand(unsigned v, unsigned max)
{
    return (uint8_t)((v * 255u + max / 2u) / max);
}

bool ppm_dump(const char *path, const uint16_t *pixels, int w, int h)
{
    if (path == NULL || pixels == NULL || w <= 0 || h <= 0) {
        return false;
    }
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    bool ok = fprintf(f, "P6\n%d %d\n255\n", w, h) > 0;
    for (long i = 0; ok && i < (long)w * h; i++) {
        unsigned p = pixels[i];
        uint8_t rgb[3] = {expand((p >> 11) & 0x1Fu, 31u), expand((p >> 5) & 0x3Fu, 63u),
                          expand(p & 0x1Fu, 31u)};
        ok = fwrite(rgb, 1, sizeof rgb, f) == sizeof rgb;
    }
    return fclose(f) == 0 && ok;
}
