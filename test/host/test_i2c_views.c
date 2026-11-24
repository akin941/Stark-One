/*
 * test_i2c_views.c — the I2C app's pure view logic (apps/app_i2c/i2c_views.c, TASKS.md STARK-0704):
 * the rows of the read and write editors, the text each shows, what LEFT and RIGHT do to a nibble,
 * a length, a count and a byte (wrapping inside a nibble, clamping at the ends), the dump rows and
 * the confirmation texts. No service call, no screen.
 */
#include <stdio.h>
#include <string.h>
#include "i2capp.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static i2capp_cfg_t cfg(void)
{
    i2capp_cfg_t c;
    i2cview_cfg_reset(&c);
    return c;
}

static const char *value(i2cview_kind_t kind, uint8_t idx, const i2capp_cfg_t *c, char *buf)
{
    const i2cview_row_t row = {kind, idx};
    const uint8_t result[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    return i2cview_value(&row, c, result, 12, "", buf, 40);
}

static void test_the_reset_is_register_zero_8_bit_one_byte(void)
{
    i2capp_cfg_t c = cfg();
    TEST_ASSERT_EQUAL_HEX16(0, c.reg);
    TEST_ASSERT_FALSE(c.reg16);
    TEST_ASSERT_TRUE(c.low_nibble);
    TEST_ASSERT_EQUAL_UINT8(1, c.len);
    TEST_ASSERT_EQUAL_UINT8(1, c.count);
    for (size_t i = 0; i < I2CAPP_WRITE_MAX; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, c.data[i]);
    }
}

static void test_the_read_editor_rows(void)
{
    i2capp_cfg_t c = cfg();
    i2cview_row_t r[I2CAPP_ROWS_MAX];
    size_t n = i2cview_rows(I2CAPP_EDIT_READ, &c, 0, r);
    TEST_ASSERT_EQUAL_UINT(6, n); /* Reg, Register, Nibble, Length, Read, note */
    const i2cview_kind_t want[] = {I2CROW_REG, I2CROW_WIDTH, I2CROW_NIBBLE,
                                   I2CROW_LEN, I2CROW_READ,  I2CROW_NOTE};
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_INT(want[i], r[i].kind);
    }
    c.reg16 = true;
    n = i2cview_rows(I2CAPP_EDIT_READ, &c, 1, r);
    TEST_ASSERT_EQUAL_UINT(8, n); /* the high byte and one result row more */
    TEST_ASSERT_EQUAL_INT(I2CROW_REG_HI, r[1].kind);
    TEST_ASSERT_EQUAL_INT(I2CROW_RESULT, r[7].kind);
    TEST_ASSERT_EQUAL_UINT8(0, r[7].idx);
    TEST_ASSERT_EQUAL_UINT(8, i2cview_rows(I2CAPP_EDIT_READ, &c, 8, r)); /* eight bytes: one row */
    TEST_ASSERT_EQUAL_UINT(9, i2cview_rows(I2CAPP_EDIT_READ, &c, 9, r)); /* nine: two */
    TEST_ASSERT_EQUAL_UINT8(1, r[8].idx);
    TEST_ASSERT_EQUAL_UINT(9, i2cview_rows(I2CAPP_EDIT_READ, &c, 16, r));
    TEST_ASSERT_EQUAL_UINT(9, i2cview_rows(I2CAPP_EDIT_READ, &c, 99, r)); /* never more than two */
}

static void test_the_write_editor_rows_follow_the_count(void)
{
    i2capp_cfg_t c = cfg();
    i2cview_row_t r[I2CAPP_ROWS_MAX];
    for (uint8_t count = 1; count <= 8; count++) {
        c.count = count;
        size_t n = i2cview_rows(I2CAPP_EDIT_WRITE, &c, 0, r);
        TEST_ASSERT_EQUAL_UINT(3u + 1u + count + 2u,
                               n); /* Reg, Register, Nibble, Bytes, bytes, Write, note */
        TEST_ASSERT_EQUAL_INT(I2CROW_COUNT, r[3].kind);
        TEST_ASSERT_EQUAL_INT(I2CROW_BYTE, r[4].kind);
        TEST_ASSERT_EQUAL_INT(I2CROW_WRITE, r[n - 2u].kind);
        TEST_ASSERT_EQUAL_INT(I2CROW_NOTE, r[n - 1u].kind);
        TEST_ASSERT_TRUE(n <= I2CAPP_ROWS_MAX);
    }
    c.count = 8;
    c.reg16 = true;
    TEST_ASSERT_EQUAL_UINT(15, i2cview_rows(I2CAPP_EDIT_WRITE, &c, 0, r)); /* the longest editor */
    c.count = 200; /* hostile: bounded by the table */
    TEST_ASSERT_EQUAL_UINT(15, i2cview_rows(I2CAPP_EDIT_WRITE, &c, 0, r));
    TEST_ASSERT_TRUE(15u <= I2CAPP_ROWS_MAX);
}

static void test_labels(void)
{
    const struct {
        i2cview_kind_t kind;
        uint8_t idx;
        const char *text;
    } want[] = {
        {I2CROW_REG, 0, "Reg"},
        {I2CROW_REG_HI, 0, "Reg high"},
        {I2CROW_WIDTH, 0, "Register"},
        {I2CROW_NIBBLE, 0, "Nibble"},
        {I2CROW_LEN, 0, "Length"},
        {I2CROW_COUNT, 0, "Bytes"},
        {I2CROW_BYTE, 0, "Byte 0"},
        {I2CROW_BYTE, 7, "Byte 7"},
        {I2CROW_BYTE, 8, "Byte 0"},
        {I2CROW_READ, 0, "Read"},
        {I2CROW_WRITE, 0, "Write"},
        {I2CROW_RESULT, 0, "Bytes 0-7"},
        {I2CROW_RESULT, 1, "Bytes 8-15"},
        {I2CROW_RESULT, 2, "Bytes 0-7"},
        {I2CROW_NOTE, 0, ""},
    };
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        const i2cview_row_t row = {want[i].kind, want[i].idx};
        TEST_ASSERT_EQUAL_STRING(want[i].text, i2cview_label(&row));
    }
    const i2cview_row_t unknown = {(i2cview_kind_t)99, 0};
    TEST_ASSERT_EQUAL_STRING("", i2cview_label(&unknown));
    TEST_ASSERT_EQUAL_STRING("A read can change a device's state", i2cview_note(I2CAPP_EDIT_READ));
    TEST_ASSERT_EQUAL_STRING("Own hardware only", i2cview_note(I2CAPP_EDIT_WRITE));
}

static void test_values(void)
{
    char buf[40];
    i2capp_cfg_t c = cfg();
    c.reg = 0x126b;
    c.reg16 = true;
    c.len = 12;
    c.count = 3;
    c.data[2] = 0xa5;
    TEST_ASSERT_EQUAL_STRING("0x6b", value(I2CROW_REG, 0, &c, buf));
    TEST_ASSERT_EQUAL_STRING("0x12", value(I2CROW_REG_HI, 0, &c, buf));
    TEST_ASSERT_EQUAL_STRING("16-bit", value(I2CROW_WIDTH, 0, &c, buf));
    c.reg16 = false;
    TEST_ASSERT_EQUAL_STRING("8-bit", value(I2CROW_WIDTH, 0, &c, buf));
    TEST_ASSERT_EQUAL_STRING("low", value(I2CROW_NIBBLE, 0, &c, buf));
    c.low_nibble = false;
    TEST_ASSERT_EQUAL_STRING("high", value(I2CROW_NIBBLE, 0, &c, buf));
    TEST_ASSERT_EQUAL_STRING("12", value(I2CROW_LEN, 0, &c, buf));
    TEST_ASSERT_EQUAL_STRING("3", value(I2CROW_COUNT, 0, &c, buf));
    TEST_ASSERT_EQUAL_STRING("0xa5", value(I2CROW_BYTE, 2, &c, buf));
    TEST_ASSERT_EQUAL_STRING("0x00", value(I2CROW_BYTE, 0, &c, buf));
    TEST_ASSERT_EQUAL_STRING(
        "0xa5", value(I2CROW_BYTE, 10, &c, buf)); /* the index wraps, never reads past */
    TEST_ASSERT_NULL(value(I2CROW_NOTE, 0, &c, buf));
    TEST_ASSERT_NULL(value(I2CROW_READ, 0, &c, buf));
    TEST_ASSERT_NULL(value(I2CROW_WRITE, 0, &c, buf));
    /* the result: 12 bytes of the 16 are valid, eight per row */
    TEST_ASSERT_EQUAL_STRING("00 11 22 33 44 55 66 77", value(I2CROW_RESULT, 0, &c, buf));
    TEST_ASSERT_EQUAL_STRING("88 99 aa bb", value(I2CROW_RESULT, 1, &c, buf));
    TEST_ASSERT_EQUAL_STRING("", value(I2CROW_RESULT, 5, &c, buf));
    /* an action row shows its status */
    const i2cview_row_t read = {I2CROW_READ, 0};
    const i2cview_row_t write = {I2CROW_WRITE, 0};
    TEST_ASSERT_EQUAL_STRING("no answer", i2cview_value(&read, &c, NULL, 0, "no answer", buf, 40));
    TEST_ASSERT_EQUAL_STRING("ok", i2cview_value(&write, &c, NULL, 0, "ok", buf, 40));
}

static void test_a_nibble_steps_inside_itself(void)
{
    i2capp_cfg_t c = cfg();
    const i2cview_row_t reg = {I2CROW_REG, 0};
    const i2cview_row_t hi = {I2CROW_REG_HI, 0};
    TEST_ASSERT_TRUE(i2cview_adjust(&c, &reg, 1));
    TEST_ASSERT_EQUAL_HEX16(0x01, c.reg);
    for (int i = 0; i < 14; i++) {
        (void)i2cview_adjust(&c, &reg, 1);
    }
    TEST_ASSERT_EQUAL_HEX16(0x0f, c.reg);
    (void)i2cview_adjust(&c, &reg, 1); /* the low nibble wraps and does not carry */
    TEST_ASSERT_EQUAL_HEX16(0x00, c.reg);
    (void)i2cview_adjust(&c, &reg, -1);
    TEST_ASSERT_EQUAL_HEX16(0x0f, c.reg);
    c.low_nibble = false;
    (void)i2cview_adjust(&c, &reg, 1); /* the high nibble: 0x0f -> 0x1f */
    TEST_ASSERT_EQUAL_HEX16(0x1f, c.reg);
    (void)i2cview_adjust(&c, &reg, -1);
    (void)i2cview_adjust(&c, &reg, -1);
    TEST_ASSERT_EQUAL_HEX16(0xff, c.reg);
    c.reg16 = true;
    (void)i2cview_adjust(&c, &hi, 1); /* the high byte, high nibble */
    TEST_ASSERT_EQUAL_HEX16(0x10ff, c.reg);
    c.low_nibble = true;
    (void)i2cview_adjust(&c, &hi, -1);
    TEST_ASSERT_EQUAL_HEX16(0x1fff, c.reg);
    (void)i2cview_adjust(&c, &reg, 1); /* the low byte alone moves */
    TEST_ASSERT_EQUAL_HEX16(0x1ff0, c.reg);
    TEST_ASSERT_FALSE(i2cview_adjust(&c, &reg, 0));
}

/* The direction that reaches `want` from `have` in fewer steps on a ring of sixteen. */
static int shorter_way(unsigned have, unsigned want)
{
    return ((want - have) & 0xFu) <= 8u ? 1 : -1;
}

static void test_every_byte_is_reachable_in_a_few_steps(void)
{
    /* high nibble then low: at most 8 + 8 steps by wrapping either way */
    for (unsigned target = 0; target < 256; target++) {
        i2capp_cfg_t c = cfg();
        const i2cview_row_t b = {I2CROW_BYTE, 3};
        unsigned steps = 0;
        c.low_nibble = false;
        while ((unsigned)(c.data[3] >> 4) != (target >> 4)) {
            TEST_ASSERT_TRUE(
                i2cview_adjust(&c, &b, shorter_way((unsigned)(c.data[3] >> 4), target >> 4)));
            steps++;
        }
        c.low_nibble = true;
        while ((unsigned)(c.data[3] & 0xFu) != (target & 0xFu)) {
            TEST_ASSERT_TRUE(
                i2cview_adjust(&c, &b, shorter_way((unsigned)(c.data[3] & 0xFu), target & 0xFu)));
            steps++;
        }
        TEST_ASSERT_EQUAL_HEX8(target, c.data[3]);
        TEST_ASSERT_TRUE(steps <= 16u);
        TEST_ASSERT_EQUAL_HEX8(0, c.data[2]); /* the neighbours are untouched */
        TEST_ASSERT_EQUAL_HEX8(0, c.data[4]);
    }
}

static void test_width_toggles_and_an_8_bit_register_has_no_high_byte(void)
{
    i2capp_cfg_t c = cfg();
    const i2cview_row_t w = {I2CROW_WIDTH, 0};
    TEST_ASSERT_TRUE(i2cview_adjust(&c, &w, 1));
    TEST_ASSERT_TRUE(c.reg16);
    c.reg = 0x4321;
    TEST_ASSERT_TRUE(i2cview_adjust(&c, &w, -1));
    TEST_ASSERT_FALSE(c.reg16);
    TEST_ASSERT_EQUAL_HEX16(0x0021, c.reg);
    const i2cview_row_t n = {I2CROW_NIBBLE, 0};
    TEST_ASSERT_TRUE(i2cview_adjust(&c, &n, 1));
    TEST_ASSERT_FALSE(c.low_nibble);
    TEST_ASSERT_TRUE(i2cview_adjust(&c, &n, 1));
    TEST_ASSERT_TRUE(c.low_nibble);
}

static void test_length_and_count_clamp(void)
{
    i2capp_cfg_t c = cfg();
    const i2cview_row_t len = {I2CROW_LEN, 0};
    const i2cview_row_t cnt = {I2CROW_COUNT, 0};
    TEST_ASSERT_FALSE(i2cview_adjust(&c, &len, -1)); /* 1 is the floor */
    TEST_ASSERT_EQUAL_UINT8(1, c.len);
    for (int i = 0; i < 15; i++) {
        TEST_ASSERT_TRUE(i2cview_adjust(&c, &len, 1));
    }
    TEST_ASSERT_EQUAL_UINT8(16, c.len);
    TEST_ASSERT_FALSE(i2cview_adjust(&c, &len, 1)); /* 16 is the ceiling */
    TEST_ASSERT_TRUE(i2cview_adjust(&c, &len, -1));
    TEST_ASSERT_EQUAL_UINT8(15, c.len);
    TEST_ASSERT_FALSE(i2cview_adjust(&c, &cnt, -1));
    for (int i = 0; i < 7; i++) {
        TEST_ASSERT_TRUE(i2cview_adjust(&c, &cnt, 1));
    }
    TEST_ASSERT_EQUAL_UINT8(8, c.count);
    TEST_ASSERT_FALSE(i2cview_adjust(&c, &cnt, 1));
    TEST_ASSERT_TRUE(i2cview_adjust(&c, &cnt, -1));
    TEST_ASSERT_EQUAL_UINT8(7, c.count);
}

static void test_action_note_and_result_rows_do_not_adjust(void)
{
    i2capp_cfg_t c = cfg();
    const i2cview_kind_t kinds[] = {I2CROW_READ, I2CROW_WRITE, I2CROW_NOTE, I2CROW_RESULT,
                                    (i2cview_kind_t)99};
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        const i2cview_row_t row = {kinds[i], 0};
        TEST_ASSERT_FALSE(i2cview_adjust(&c, &row, 1));
        TEST_ASSERT_FALSE(i2cview_adjust(&c, &row, -1));
    }
    const i2capp_cfg_t fresh = cfg();
    TEST_ASSERT_EQUAL_MEMORY(&fresh, &c, sizeof c);
}

static void test_speed(void)
{
    TEST_ASSERT_EQUAL_STRING("100 kHz", i2cview_speed_text(STARK_LAB_I2C_SPEED_100K));
    TEST_ASSERT_EQUAL_STRING("400 kHz", i2cview_speed_text(STARK_LAB_I2C_SPEED_400K));
    TEST_ASSERT_EQUAL_STRING("100 kHz", i2cview_speed_text(12345));
    TEST_ASSERT_EQUAL_UINT32(STARK_LAB_I2C_SPEED_400K,
                             i2cview_speed_step(STARK_LAB_I2C_SPEED_100K));
    TEST_ASSERT_EQUAL_UINT32(STARK_LAB_I2C_SPEED_100K,
                             i2cview_speed_step(STARK_LAB_I2C_SPEED_400K));
    TEST_ASSERT_EQUAL_UINT32(STARK_LAB_I2C_SPEED_400K, i2cview_speed_step(7));
}

static void test_addresses_registers_and_dump_rows(void)
{
    char buf[40];
    i2cview_addr_text(0x08, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("0x08", buf);
    i2cview_addr_text(0x77, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("0x77", buf);
    i2cview_reg_text(0x6b, false, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("0x6b", buf);
    i2cview_reg_text(0x016b, true, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("0x016b", buf);
    i2cview_reg_text(0x016b, false, buf, sizeof buf); /* an 8-bit register shows its low byte */
    TEST_ASSERT_EQUAL_STRING("0x6b", buf);
    uint8_t dump[256];
    for (size_t i = 0; i < sizeof dump; i++) {
        dump[i] = (uint8_t)i;
    }
    i2cview_dump_label(0, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("00", buf);
    i2cview_dump_label(15, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("f0", buf);
    i2cview_dump_value(dump, 0, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("000102030405060708090a0b0c0d0e0f", buf);
    i2cview_dump_value(dump, 15, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", buf);
    i2cview_dump_value(dump, 16, buf, sizeof buf); /* the row index wraps, never reads past 256 */
    TEST_ASSERT_EQUAL_STRING("000102030405060708090a0b0c0d0e0f", buf);
}

static void test_the_confirmation_texts_name_what_will_happen(void)
{
    char msg[160];
    i2capp_cfg_t c = cfg();
    c.reg = 0x6b;
    c.count = 2;
    c.data[0] = 0x01;
    c.data[1] = 0xff;
    i2cview_write_message(0x68, &c, msg, sizeof msg);
    TEST_ASSERT_EQUAL_STRING("Write 0x68 reg 0x6b: 01 ff. Own hardware only.", msg);
    c.reg16 = true;
    c.reg = 0x0123;
    c.count = 8;
    memset(c.data, 0xab, sizeof c.data);
    i2cview_write_message(0x77, &c, msg, sizeof msg);
    TEST_ASSERT_EQUAL_STRING("Write 0x77 reg 0x0123: ab ab ab ab ab ab ab ab. Own hardware only.",
                             msg);
    TEST_ASSERT_TRUE(strlen(msg) <= 159u); /* a dialog takes 159 bytes */
    i2cview_dump_message(0x50, msg, sizeof msg);
    TEST_ASSERT_EQUAL_STRING("Dump 0x50: reads 256 bytes. A read can change a device's state.",
                             msg);
}

static void test_error_texts(void)
{
    TEST_ASSERT_EQUAL_STRING("no answer", i2cview_error_text(STARK_ERR_NOT_FOUND));
    TEST_ASSERT_EQUAL_STRING("timeout", i2cview_error_text(STARK_ERR_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("refused", i2cview_error_text(STARK_ERR_NOT_SUPPORTED));
    TEST_ASSERT_EQUAL_STRING("bad value", i2cview_error_text(STARK_ERR_INVALID_ARG));
    TEST_ASSERT_EQUAL_STRING("not confirmed", i2cview_error_text(STARK_ERR_STATE));
    TEST_ASSERT_EQUAL_STRING("bus error", i2cview_error_text(STARK_ERR_IO));
    TEST_ASSERT_EQUAL_STRING("bus error", i2cview_error_text(STARK_ERR_BUSY));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_reset_is_register_zero_8_bit_one_byte);
    RUN_TEST(test_the_read_editor_rows);
    RUN_TEST(test_the_write_editor_rows_follow_the_count);
    RUN_TEST(test_labels);
    RUN_TEST(test_values);
    RUN_TEST(test_a_nibble_steps_inside_itself);
    RUN_TEST(test_every_byte_is_reachable_in_a_few_steps);
    RUN_TEST(test_width_toggles_and_an_8_bit_register_has_no_high_byte);
    RUN_TEST(test_length_and_count_clamp);
    RUN_TEST(test_action_note_and_result_rows_do_not_adjust);
    RUN_TEST(test_speed);
    RUN_TEST(test_addresses_registers_and_dump_rows);
    RUN_TEST(test_the_confirmation_texts_name_what_will_happen);
    RUN_TEST(test_error_texts);
    return UNITY_END();
}
