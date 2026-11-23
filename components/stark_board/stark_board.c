/*
 * stark_board.c — board-agnostic pin validation.
 * Pure C, no ESP-IDF dependency: host-testable. Concrete board variants
 * (e.g. board_devkitc1.c) own the actual pin table and stark_board_init().
 */
#include "stark_board.h"
#include <stdbool.h>
#include <stddef.h>

stark_err_t stark_board_validate(const stark_board_pins_t *pins)
{
    if (pins == NULL) {
        return STARK_ERR_INVALID_ARG;
    }

    /* Every scalar/array pin field, upper-bounded generously. */
    int assigned[7 + STARK_KEY_COUNT + 9 + 4 * STARK_BOARD_MODULE_SLOTS];
    int count = 0;

#define STARK_BOARD_COLLECT(pin)                                                                   \
    do {                                                                                           \
        if ((pin) != -1) {                                                                         \
            assigned[count++] = (pin);                                                             \
        }                                                                                          \
    } while (0)

    STARK_BOARD_COLLECT(pins->sclk);
    STARK_BOARD_COLLECT(pins->mosi);
    STARK_BOARD_COLLECT(pins->miso);
    STARK_BOARD_COLLECT(pins->tft_cs);
    STARK_BOARD_COLLECT(pins->tft_dc);
    STARK_BOARD_COLLECT(pins->tft_rst);
    STARK_BOARD_COLLECT(pins->tft_bl);
    for (int i = 0; i < STARK_KEY_COUNT; i++) {
        STARK_BOARD_COLLECT(pins->key[i]);
    }
    STARK_BOARD_COLLECT(pins->buzzer);
    STARK_BOARD_COLLECT(pins->led_status);
    STARK_BOARD_COLLECT(pins->sd_cs);
    STARK_BOARD_COLLECT(pins->i2c_sda);
    STARK_BOARD_COLLECT(pins->i2c_scl);
    STARK_BOARD_COLLECT(pins->ir_tx);
    STARK_BOARD_COLLECT(pins->ir_rx);
    STARK_BOARD_COLLECT(pins->lab_uart_tx);
    STARK_BOARD_COLLECT(pins->lab_uart_rx);
    for (int i = 0; i < STARK_BOARD_MODULE_SLOTS; i++) {
        STARK_BOARD_COLLECT(pins->module[i].cs);
        STARK_BOARD_COLLECT(pins->module[i].irq0);
        STARK_BOARD_COLLECT(pins->module[i].irq1);
        STARK_BOARD_COLLECT(pins->module[i].en);
    }

#undef STARK_BOARD_COLLECT

    /* Collision: no two assigned pins may share a GPIO number. */
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (assigned[i] == assigned[j]) {
                return STARK_ERR_INVALID_ARG;
            }
        }
    }

    /* Reserved ranges: 26-37 (flash/PSRAM), 19-20 (USB), 43-46 (UART0 +
     * strapping). See stark_board.h and TASKS.md STARK-0006. */
    for (int i = 0; i < count; i++) {
        int p = assigned[i];
        bool reserved = (p >= 26 && p <= 37) || (p >= 19 && p <= 20) || (p >= 43 && p <= 46);
        if (reserved) {
            return STARK_ERR_NOT_SUPPORTED;
        }
    }

    return STARK_OK;
}

/* ---- Pin safety (STARK-0701, ADR-0023 §3) ----------------------------------- */

/* The ESP32-S3's GPIO numbers; anything outside is RESERVED without being looked up. */
#define STARK_BOARD_GPIO_MAX 48

/* NOTE: board-neutral names. Slot 0's lines are SUBGHZ_CS, SUBGHZ_GDO0 and SUBGHZ_GDO2 in
 * HARDWARE.md §2.2, but a lab tool offers them as the slot's generic wires. */
static const char *const k_slot_line_names[STARK_BOARD_MODULE_SLOTS][3] = {
    {"MOD0_CS", "MOD0_IRQ0", "MOD0_IRQ1"},
    {"MOD1_CS", "MOD1_IRQ0", "MOD1_IRQ1"},
};

size_t stark_board_lab_pins_of(const stark_board_pins_t *pins, stark_board_lab_pin_t *out,
                               size_t max)
{
    if (pins == NULL) {
        return 0;
    }
    size_t n = 0;

#define STARK_BOARD_OFFER(gpio_, name_, cls_, slot_)                                               \
    do {                                                                                           \
        if ((gpio_) >= 0 && (gpio_) <= STARK_BOARD_GPIO_MAX) {                                     \
            if (out != NULL && n < max) {                                                          \
                out[n] = (stark_board_lab_pin_t){(gpio_), (name_), (cls_), (slot_)};               \
            }                                                                                      \
            n++;                                                                                   \
        }                                                                                          \
    } while (0)

    STARK_BOARD_OFFER(pins->lab_uart_tx, "LAB_UART_TX", STARK_PIN_LAB, -1);
    STARK_BOARD_OFFER(pins->lab_uart_rx, "LAB_UART_RX", STARK_PIN_LAB, -1);
    for (int i = 0; i < STARK_BOARD_MODULE_SLOTS; i++) {
        STARK_BOARD_OFFER(pins->module[i].cs, k_slot_line_names[i][0], STARK_PIN_SLOT, i);
        STARK_BOARD_OFFER(pins->module[i].irq0, k_slot_line_names[i][1], STARK_PIN_SLOT, i);
        STARK_BOARD_OFFER(pins->module[i].irq1, k_slot_line_names[i][2], STARK_PIN_SLOT, i);
    }
#undef STARK_BOARD_OFFER
    return n;
}

stark_pin_class_t stark_board_pin_class_of(const stark_board_pins_t *pins, int gpio, int *slot)
{
    if (slot != NULL) {
        *slot = -1;
    }
    stark_board_lab_pin_t tab[STARK_BOARD_LAB_PINS_MAX];
    size_t n = stark_board_lab_pins_of(pins, tab, STARK_BOARD_LAB_PINS_MAX);
    for (size_t i = 0; i < n; i++) {
        if (tab[i].gpio == gpio) {
            if (slot != NULL) {
                *slot = tab[i].slot;
            }
            return tab[i].cls;
        }
    }
    return STARK_PIN_RESERVED;
}

stark_pin_class_t stark_board_pin_class(int gpio, int *slot)
{
    return stark_board_pin_class_of(stark_board_pins(), gpio, slot);
}

size_t stark_board_lab_pins(stark_board_lab_pin_t *out, size_t max)
{
    return stark_board_lab_pins_of(stark_board_pins(), out, max);
}
