/*
 * stark_board.h — L0, sole source of truth for hardware wiring.
 * Pin values are normative in HARDWARE.md §2; nothing outside this
 * component may name a GPIO (AGENTS.md).
 */
#pragma once

#include <stdint.h>
#include "stark_err.h"

/*
 * Number of physical keys on any STARK ONE board (UP DOWN LEFT RIGHT OK
 * BACK). This is a board-level hardware fact (how many key GPIOs the board
 * wires up), so it is owned here rather than in stark_input (which does
 * not exist yet). When stark_input's key enum is implemented, it must
 * reuse STARK_KEY_COUNT — e.g.
 *   _Static_assert(STARK_KEY_COUNT == STARK_KEY_BACK + 1, "...");
 * — rather than redefine it, so the two can never silently drift apart.
 */
#define STARK_KEY_COUNT 6

typedef struct {
    int sclk, mosi, miso; /* shared SPI2 bus */
    int tft_cs, tft_dc, tft_rst, tft_bl;
    int key[STARK_KEY_COUNT]; /* UP DOWN LEFT RIGHT OK BACK */
    int buzzer, led_status;
    int sd_cs;            /* -1 when absent */
    int i2c_sda, i2c_scl; /* -1 when absent */
    uint32_t tft_spi_hz;
} stark_board_pins_t;

/* Returns the active board variant's pin map (selected by Kconfig). */
const stark_board_pins_t *stark_board_pins(void);

/* Returns the active board variant's name, e.g. "devkitc1". */
const char *stark_board_name(void);

/*
 * Configures the status LED as output, the six key GPIOs as inputs with
 * pull-ups, and initialises the shared SPI2 bus. Does not touch the panel.
 * Validates the pin map first (see stark_board_validate()) and returns
 * immediately on failure without touching any GPIO — it does not log and
 * it does not panic (no such facility has an owner yet; L0 must not
 * depend on stark_log — see ARCHITECTURE.md §2). The caller decides what
 * a failure means; see main.c.
 */
stark_err_t stark_board_init(void);

/*
 * Pure validation: rejects a pin map where two assigned (non -1) pins
 * collide, or where any assigned pin falls in the reserved ranges
 * 26-37 (internal flash/PSRAM), 19-20 (USB D-/D+), 43-46 (UART0 console
 * + two strapping pins) — TASKS.md STARK-0006. No ESP-IDF dependency —
 * host-testable.
 */
stark_err_t stark_board_validate(const stark_board_pins_t *pins);
