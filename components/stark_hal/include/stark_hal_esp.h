/*
 * stark_hal_esp.h — ESP-IDF error translation. Target-only, but public.
 *
 * Deliberately separate from stark_hal.h: esp_err_t is the ESP-IDF type
 * stark_err_from_esp() translates *from*, so it cannot be declared
 * without esp_err.h — and esp_err.h does not exist on the host build.
 * "esp_err_t never crosses a public STARK header" (ARCHITECTURE.md §5)
 * means unrelated headers, not this one: this is the one function whose
 * whole job is to be the boundary where esp_err_t enters stark_err_t.
 *
 * No host counterpart exists (or is meaningful — there is no host
 * esp_err_t to translate), so this is not part of the host/target
 * shared-header contract STARK-0007 AC #1 is about.
 *
 * Public, not a private helper hidden inside stark_hal_esp.c: ARCHITECTURE
 * §5 says "ports translate at the boundary (stark_err_from_esp() lives in
 * stark_hal)" — the boundary in question is every future ESP-IDF-facing
 * port file (stark_display's, stark_input's, ...), not just stark_hal's
 * own. Those are all L2+/L3, so depending on stark_hal (L1) is layering-
 * legal (ARCHITECTURE.md §2); this header lives in components/stark_hal/
 * include/ specifically so any component that REQUIRES stark_hal can
 * reach it. (components/stark_board/board_devkitc1.c is the one
 * exception: it's L0, below stark_hal, so it cannot call this — see the
 * STARK-0007 report for why its ESP-IDF failures still return a generic
 * code instead.)
 */
#pragma once

#include "esp_err.h"
#include "stark_err.h"

/*
 * Deterministic esp_err_t -> stark_err_t mapping (see stark_hal_esp.c for
 * the full table). Every named "core" esp_err_t constant in ESP-IDF
 * v6.1's esp_err.h (ESP_OK, ESP_FAIL, ESP_ERR_NO_MEM .. ESP_ERR_NOT_ALLOWED)
 * has an explicit, deliberate case — none fall through by omission.
 *
 * Anything else — subsystem-specific codes (Wi-Fi, mesh, flash, ...)
 * STARK ONE does not use in V0, and any future ESP-IDF error this mapping
 * has not been updated for — falls back to STARK_ERR_IO: every stark_hal
 * caller is wrapping a hardware/driver operation, so "something went
 * wrong at the hardware boundary" is the correct generic bucket for an
 * error this table cannot yet name, not a guess at a more specific one.
 */
stark_err_t stark_err_from_esp(esp_err_t err);
