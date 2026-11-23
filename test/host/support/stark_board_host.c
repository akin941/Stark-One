/*
 * stark_board_host.c — a placeholder for stark_board_pins() on the host. stark_board.c's
 * wrappers (stark_board_pin_class(), stark_board_lab_pins()) read the active board's map, which
 * lives in the ESP-IDF-only board_devkitc1.c; targets that link stark_board.c but exercise only
 * its pure, map-taking functions link this all-unassigned map instead.
 */
#include "stark_board.h"

static const stark_board_pins_t s_none = {
    .lab_uart_tx = -1,
    .lab_uart_rx = -1,
    .module = {{.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1},
               {.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1}},
};

const stark_board_pins_t *stark_board_pins(void)
{
    return &s_none;
}
