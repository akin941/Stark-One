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
        .tft_spi_hz = 20000000,
    };
    return p;
}

void test_stark_board_validate_accepts_good_map(void)
{
    stark_board_pins_t p = good_map();
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
    RUN_TEST(test_stark_board_validate_rejects_reserved_pin);
    RUN_TEST(test_stark_board_validate_rejects_null);
    return UNITY_END();
}
