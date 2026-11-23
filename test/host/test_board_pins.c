/*
 * test_board_pins.c — pin classes and the lab's offer table (TASKS.md STARK-0701,
 * ADR-0023 §3, HARDWARE.md §2.4), on the pure half of stark_board. The classification is
 * derived from the pin map, so the tests change the map and watch the classes move.
 */
#include "stark_board.h"
#include "unity.h"

/* HARDWARE.md §2's map (what board_devkitc1.c holds), with the lab and slot-0 lines. */
static stark_board_pins_t s_map;

const stark_board_pins_t *stark_board_pins(void)
{
    return &s_map;
}

static stark_board_pins_t board_map(void)
{
    stark_board_pins_t p = {
        .sclk = 12,
        .mosi = 11,
        .miso = 13,
        .tft_cs = 10,
        .tft_dc = 9,
        .tft_rst = 14,
        .tft_bl = 21,
        .key = {4, 5, 6, 7, 15, 16},
        .buzzer = 17,
        .led_status = 18,
        .sd_cs = 8,
        .i2c_sda = 1,
        .i2c_scl = 2,
        .ir_tx = 47,
        .ir_rx = 48,
        .lab_uart_tx = 38,
        .lab_uart_rx = 39,
        .tft_spi_hz = 20000000,
        .module = {{.cs = 40, .irq0 = 41, .irq1 = 42, .en = -1, .eeprom_addr = 0x50},
                   {.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1, .eeprom_addr = 0x51}},
    };
    return p;
}

void setUp(void)
{
    s_map = board_map();
}
void tearDown(void)
{}

static bool offered_number(int gpio)
{
    return stark_board_pin_class_of(&s_map, gpio, NULL) != STARK_PIN_RESERVED;
}

/* AC 1: only 38 to 42 are anything but RESERVED, for every number from -1 to 64. */
static void test_class_of_every_number(void)
{
    for (int gpio = -1; gpio <= 64; gpio++) {
        int slot = 99;
        stark_pin_class_t cls = stark_board_pin_class_of(&s_map, gpio, &slot);
        if (gpio == 38 || gpio == 39) {
            TEST_ASSERT_EQUAL_MESSAGE(STARK_PIN_LAB, cls, "lab pin");
            TEST_ASSERT_EQUAL(-1, slot);
        } else if (gpio >= 40 && gpio <= 42) {
            TEST_ASSERT_EQUAL_MESSAGE(STARK_PIN_SLOT, cls, "slot pin");
            TEST_ASSERT_EQUAL(0, slot);
        } else {
            TEST_ASSERT_EQUAL_MESSAGE(STARK_PIN_RESERVED, cls, "everything else");
            TEST_ASSERT_EQUAL(-1, slot);
        }
    }
}

/* Every pin another owner has is RESERVED: the display, SD, keys, buzzer, LED, I2C, IR. */
static void test_every_owned_pin_is_reserved(void)
{
    const int owned[] = {s_map.sclk,       s_map.mosi,    s_map.miso,    s_map.tft_cs,
                         s_map.tft_dc,     s_map.tft_rst, s_map.tft_bl,  s_map.buzzer,
                         s_map.led_status, s_map.sd_cs,   s_map.i2c_sda, s_map.i2c_scl,
                         s_map.ir_tx,      s_map.ir_rx,   s_map.key[0],  s_map.key[1],
                         s_map.key[2],     s_map.key[3],  s_map.key[4],  s_map.key[5]};
    for (size_t i = 0; i < sizeof(owned) / sizeof(owned[0]); i++) {
        TEST_ASSERT_FALSE(offered_number(owned[i]));
    }
    /* flash/PSRAM 26-37, USB 19-20, console and strapping 43-46, boot strapping 0, 3, 45, 46 */
    for (int gpio = 19; gpio <= 37; gpio++) {
        TEST_ASSERT_FALSE(offered_number(gpio));
    }
    for (int gpio = 43; gpio <= 46; gpio++) {
        TEST_ASSERT_FALSE(offered_number(gpio));
    }
    TEST_ASSERT_FALSE(offered_number(0));
    TEST_ASSERT_FALSE(offered_number(3));
}

static void test_table_has_five_entries_in_order(void)
{
    stark_board_lab_pin_t tab[STARK_BOARD_LAB_PINS_MAX];
    TEST_ASSERT_EQUAL_UINT(5, stark_board_lab_pins_of(&s_map, tab, STARK_BOARD_LAB_PINS_MAX));
    const int pins[] = {38, 39, 40, 41, 42};
    const char *const names[] = {"LAB_UART_TX", "LAB_UART_RX", "MOD0_CS", "MOD0_IRQ0", "MOD0_IRQ1"};
    for (size_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(pins[i], tab[i].gpio);
        TEST_ASSERT_EQUAL_STRING(names[i], tab[i].name);
        TEST_ASSERT_EQUAL(i < 2 ? STARK_PIN_LAB : STARK_PIN_SLOT, tab[i].cls);
        TEST_ASSERT_EQUAL(i < 2 ? -1 : 0, tab[i].slot);
    }
    TEST_ASSERT_EQUAL_UINT(8, STARK_BOARD_LAB_PINS_MAX);
}

static void test_table_count_copy_and_truncation(void)
{
    TEST_ASSERT_EQUAL_UINT(5, stark_board_lab_pins_of(&s_map, NULL, 0));
    stark_board_lab_pin_t tab[2] = {{.gpio = -7}, {.gpio = -7}};
    TEST_ASSERT_EQUAL_UINT(5, stark_board_lab_pins_of(&s_map, tab, 2));
    TEST_ASSERT_EQUAL(38, tab[0].gpio);
    TEST_ASSERT_EQUAL(39, tab[1].gpio);
    TEST_ASSERT_EQUAL_UINT(0, stark_board_lab_pins_of(NULL, tab, 2));
}

/* AC 4: the table is derived from the struct — a changed pin moves the class, nothing else. */
static void test_class_follows_the_map(void)
{
    s_map.lab_uart_tx = 3;
    TEST_ASSERT_EQUAL(STARK_PIN_LAB, stark_board_pin_class_of(&s_map, 3, NULL));
    TEST_ASSERT_EQUAL(STARK_PIN_RESERVED, stark_board_pin_class_of(&s_map, 38, NULL));
    s_map = board_map();
    s_map.module[1] = (stark_board_module_slot_t){.cs = 35, .irq0 = -1, .irq1 = 36};
    int slot = -1;
    TEST_ASSERT_EQUAL(STARK_PIN_SLOT, stark_board_pin_class_of(&s_map, 35, &slot));
    TEST_ASSERT_EQUAL(1, slot);
    TEST_ASSERT_EQUAL(STARK_PIN_SLOT, stark_board_pin_class_of(&s_map, 36, &slot));
    TEST_ASSERT_EQUAL(1, slot);
    TEST_ASSERT_EQUAL_UINT(7, stark_board_lab_pins_of(&s_map, NULL, 0));
    stark_board_lab_pin_t tab[STARK_BOARD_LAB_PINS_MAX];
    (void)stark_board_lab_pins_of(&s_map, tab, STARK_BOARD_LAB_PINS_MAX);
    TEST_ASSERT_EQUAL_STRING("MOD1_CS", tab[5].name);
    TEST_ASSERT_EQUAL_STRING("MOD1_IRQ1", tab[6].name);
    s_map = board_map();
    s_map.module[0].cs = -1; /* a line the board does not wire is not offered */
    TEST_ASSERT_EQUAL(STARK_PIN_RESERVED, stark_board_pin_class_of(&s_map, 40, NULL));
    TEST_ASSERT_EQUAL_UINT(4, stark_board_lab_pins_of(&s_map, NULL, 0));
}

/* A number outside 0..48 in a map is never offered, and never a lookup hit. */
static void test_numbers_outside_the_chip_are_never_offered(void)
{
    s_map.lab_uart_tx = 49;
    s_map.lab_uart_rx = 64;
    s_map.module[0].cs = -5;
    TEST_ASSERT_EQUAL_UINT(2, stark_board_lab_pins_of(&s_map, NULL, 0));
    for (int gpio = -8; gpio <= 70; gpio++) {
        stark_pin_class_t cls = stark_board_pin_class_of(&s_map, gpio, NULL);
        TEST_ASSERT_EQUAL(gpio == 41 || gpio == 42 ? STARK_PIN_SLOT : STARK_PIN_RESERVED, cls);
    }
}

/* The wrappers read the active board's map (stark_board_pins()). */
static void test_wrappers_use_the_active_map(void)
{
    int slot = 5;
    TEST_ASSERT_EQUAL(STARK_PIN_SLOT, stark_board_pin_class(41, &slot));
    TEST_ASSERT_EQUAL(0, slot);
    TEST_ASSERT_EQUAL(STARK_PIN_RESERVED, stark_board_pin_class(10, NULL));
    stark_board_lab_pin_t tab[STARK_BOARD_LAB_PINS_MAX];
    TEST_ASSERT_EQUAL_UINT(5, stark_board_lab_pins(tab, STARK_BOARD_LAB_PINS_MAX));
    TEST_ASSERT_EQUAL(42, tab[4].gpio);
}

/* The new fields join the validator's collision, reserved-range and absent rules. */
static void test_validate_counts_the_lab_pins(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, stark_board_validate(&s_map));
    s_map.lab_uart_tx = s_map.tft_cs;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_board_validate(&s_map));
    s_map = board_map();
    s_map.lab_uart_rx = s_map.lab_uart_tx;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_board_validate(&s_map));
    s_map = board_map();
    s_map.lab_uart_rx = 30; /* flash/PSRAM */
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, stark_board_validate(&s_map));
    s_map = board_map();
    s_map.lab_uart_tx = -1;
    s_map.lab_uart_rx = -1; /* a board without lab pins */
    TEST_ASSERT_EQUAL(STARK_OK, stark_board_validate(&s_map));
    TEST_ASSERT_EQUAL_UINT(3, stark_board_lab_pins_of(&s_map, NULL, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_class_of_every_number);
    RUN_TEST(test_every_owned_pin_is_reserved);
    RUN_TEST(test_table_has_five_entries_in_order);
    RUN_TEST(test_table_count_copy_and_truncation);
    RUN_TEST(test_class_follows_the_map);
    RUN_TEST(test_numbers_outside_the_chip_are_never_offered);
    RUN_TEST(test_wrappers_use_the_active_map);
    RUN_TEST(test_validate_counts_the_lab_pins);
    return UNITY_END();
}
