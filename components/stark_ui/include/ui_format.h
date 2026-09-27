/*
 * ui_format.h — pure text formatting helpers for screens (TASKS.md
 * STARK-0020: any such helper lives in stark_ui, host-tested).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Uptime as "HH:MM:SS", or "<d>d HH:MM:SS" from one day on. Writes at most
 * size bytes, always NUL-terminated when size > 0 (truncating if needed);
 * returns the length the full text needs, like snprintf.
 */
size_t ui_format_uptime(uint64_t seconds, char *buf, size_t size);
