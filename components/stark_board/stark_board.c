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
    int assigned[7 + STARK_KEY_COUNT + 5];
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
