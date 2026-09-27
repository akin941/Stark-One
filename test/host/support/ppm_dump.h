/*
 * ppm_dump.h — host-test support: write an RGB565 buffer as a binary PPM
 * (P6) so a human can look at what a failing graphics test drew.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Returns false if the file cannot be written or w/h are not positive. */
bool ppm_dump(const char *path, const uint16_t *pixels, int w, int h);
