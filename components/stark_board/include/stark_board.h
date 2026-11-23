/*
 * stark_board.h — L0, sole source of truth for hardware wiring.
 * Pin values are normative in HARDWARE.md §2; nothing outside this
 * component may name a GPIO (AGENTS.md).
 */
#pragma once

#include <stddef.h>
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
#define STARK_KEY_COUNT          6

#define STARK_BOARD_MODULE_SLOTS 2

/* One module slot (HARDWARE.md §6); a line the board does not wire is -1. */
typedef struct {
    int cs, irq0, irq1, en;
    uint8_t eeprom_addr;
} stark_board_module_slot_t;

typedef struct {
    int sclk, mosi, miso; /* shared SPI2 bus */
    int tft_cs, tft_dc, tft_rst, tft_bl;
    int key[STARK_KEY_COUNT]; /* UP DOWN LEFT RIGHT OK BACK */
    int buzzer, led_status;
    int sd_cs;            /* -1 when absent */
    int i2c_sda, i2c_scl; /* -1 when absent */
    int ir_tx, ir_rx; /* RMT TX -> IR LED driver, RMT RX <- 38 kHz demodulator; -1 when absent */
    int lab_uart_tx, lab_uart_rx; /* the lab UART pins, the lab's always-offered pins (§2.4); -1 */
    uint32_t tft_spi_hz;
    stark_board_module_slot_t module[STARK_BOARD_MODULE_SLOTS];
} stark_board_pins_t;

/* Returns the active board variant's pin map (selected by Kconfig). */
const stark_board_pins_t *stark_board_pins(void);

/* Returns the active board variant's name, e.g. "devkitc1". */
const char *stark_board_name(void);

struct i2c_master_bus_t; /* ESP-IDF's i2c_master_bus_handle_t points to one */

/*
 * The shared I2C master bus on I2C_SDA / I2C_SCL (HARDWARE.md §2.2: NFC now,
 * module ID EEPROMs and lab I2C later), created by stark_board_init() the way
 * SPI2 is. NULL before init or when the board has no I2C pins. Devices attach
 * with i2c_master_bus_add_device() in their own port files.
 */
struct i2c_master_bus_t *stark_board_i2c_bus(void);

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
 * Starts the status LED heartbeat: 1 Hz, 10 % duty (a 100 ms blink once a
 * second), driven by a periodic esp_timer — TASKS.md STARK-0008. Call once,
 * after stark_board_init() has returned STARK_OK (it configures the LED
 * pin as an output; this does not). The heartbeat keeps running after
 * app_main() returns. Returns STARK_ERR_STATE if already started, and
 * STARK_ERR_IO if the timer cannot be created or started — in that case no
 * timer is left behind, the LED is left off, and a later call may retry.
 * Logs nothing — the caller decides.
 */
stark_err_t stark_board_heartbeat_start(void);

/*
 * Pure validation: rejects a pin map where two assigned (non -1) pins
 * collide, or where any assigned pin falls in the reserved ranges
 * 26-37 (internal flash/PSRAM), 19-20 (USB D-/D+), 43-46 (UART0 console
 * + two strapping pins) — TASKS.md STARK-0006. No ESP-IDF dependency —
 * host-testable.
 */
stark_err_t stark_board_validate(const stark_board_pins_t *pins);

/* ---- Pin safety (STARK-0701, ADR-0023 §3, HARDWARE.md §2.4) ------------------
 *
 * Which GPIOs a lab tool may touch. The classification is derived from the pin
 * map (the lab UART pins and each module slot's CS and IRQ lines); no number is
 * written a second time. Pure: host-testable.
 */
typedef enum {
    STARK_PIN_RESERVED, /* never offered, in no mode, not even to read */
    STARK_PIN_LAB,      /* LAB_UART_TX / LAB_UART_RX: always offered */
    STARK_PIN_SLOT /* a module slot's MOD_CS, MOD_IRQ0, MOD_IRQ1: only while the slot is empty */
} stark_pin_class_t;

/* The lab can name at most the two UART pins and three lines of each slot. */
#define STARK_BOARD_LAB_PINS_MAX (2 + 3 * STARK_BOARD_MODULE_SLOTS)

typedef struct {
    int gpio;
    const char *name; /* "LAB_UART_TX", "MOD0_CS", ... */
    stark_pin_class_t cls;
    int slot; /* the module slot for STARK_PIN_SLOT, else -1 */
} stark_board_lab_pin_t;

/*
 * The class of a GPIO number in a pin map. STARK_PIN_RESERVED for every number the
 * map does not offer, including every number below 0 or above 48. For
 * STARK_PIN_SLOT, *slot (when not NULL) is the module slot; otherwise -1.
 */
stark_pin_class_t stark_board_pin_class_of(const stark_board_pins_t *pins, int gpio, int *slot);

/*
 * The offer table of a pin map in the order lab_uart_tx, lab_uart_rx, then each slot's
 * cs, irq0, irq1 (lines the board does not wire are omitted). Copies at most max entries
 * and returns how many exist (at most STARK_BOARD_LAB_PINS_MAX).
 */
size_t stark_board_lab_pins_of(const stark_board_pins_t *pins, stark_board_lab_pin_t *out,
                               size_t max);

/* The same for the active board's map (stark_board_pins()). */
stark_pin_class_t stark_board_pin_class(int gpio, int *slot);
size_t stark_board_lab_pins(stark_board_lab_pin_t *out, size_t max);
