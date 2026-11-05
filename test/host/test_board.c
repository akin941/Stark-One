/*
 * test_board.c — host unit tests for stark_board_validate()
 * Compiles against stark_board.c only (the pure, board-agnostic half of
 * stark_board) — board_devkitc1.c is ESP-IDF-dependent and not linked here.
 */
#include "stark_board.h"
#include "unity.h"

/* Mirrors HARDWARE.md §2's normative DevKitC-1 map — a genuinely valid map. */
static stark_board_pins_t good_map(void)
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
        .tft_spi_hz = 20000000,
        .module = {{.cs = 40, .irq0 = 41, .irq1 = 42, .en = -1, .eeprom_addr = 0x50},
                   {.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1, .eeprom_addr = 0x51}},
    };
    return p;
}

void test_stark_board_validate_accepts_good_map(void)
{
    stark_board_pins_t p = good_map();
    TEST_ASSERT_EQUAL(STARK_OK, stark_board_validate(&p));
}

/* STARK-0503: module lines join the map's collision and reserved checks. */
void test_stark_board_validate_checks_module_slots(void)
{
    stark_board_pins_t p = good_map();
    p.module[1].cs = p.module[0].cs; /* crossed chip selects */
    TEST_ASSERT_NOT_EQUAL(STARK_OK, stark_board_validate(&p));
    p = good_map();
    p.module[0].irq0 = p.tft_cs; /* a collision with the panel */
    TEST_ASSERT_NOT_EQUAL(STARK_OK, stark_board_validate(&p));
    p = good_map();
    p.module[1].en = 30; /* reserved: flash/PSRAM */
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, stark_board_validate(&p));
    p = good_map();
    p.module[1].cs = 38; /* a second wired slot is legal when the pin is free */
    TEST_ASSERT_EQUAL(STARK_OK, stark_board_validate(&p));
}

void test_stark_board_validate_rejects_colliding_map(void)
{
    stark_board_pins_t p = good_map();
    p.buzzer = p.led_status; /* two fields, same GPIO */
    TEST_ASSERT_NOT_EQUAL(STARK_OK, stark_board_validate(&p));
}

void test_stark_board_validate_rejects_reserved_pin(void)
{
    stark_board_pins_t p = good_map();
    p.tft_bl = 30; /* inside 26-37, internal flash/PSRAM */
    TEST_ASSERT_NOT_EQUAL(STARK_OK, stark_board_validate(&p));
}

void test_stark_board_validate_checks_ir_pins(void)
{
    stark_board_pins_t p = good_map();
    p.ir_rx = p.buzzer; /* STARK-0303: IR pins join the collision check */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_board_validate(&p));
    p = good_map();
    p.ir_tx = -1;
    p.ir_rx = -1; /* a board without IR */
    TEST_ASSERT_EQUAL(STARK_OK, stark_board_validate(&p));
}

void test_stark_board_validate_rejects_null(void)
{
    TEST_ASSERT_NOT_EQUAL(STARK_OK, stark_board_validate(NULL));
}

/* Unity requires setUp and tearDown functions */
void setUp(void)
{}

void tearDown(void)
{}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stark_board_validate_accepts_good_map);
    RUN_TEST(test_stark_board_validate_rejects_colliding_map);
    RUN_TEST(test_stark_board_validate_checks_module_slots);
    RUN_TEST(test_stark_board_validate_rejects_reserved_pin);
    RUN_TEST(test_stark_board_validate_checks_ir_pins);
    RUN_TEST(test_stark_board_validate_rejects_null);
    return UNITY_END();
}
