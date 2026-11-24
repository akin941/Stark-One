/*
 * test_spi_views.c — the SPI app's pure rows and texts (apps/app_spi/spi_views.c, TASKS.md
 * STARK-0706): the default configuration and the three presets, the rows of the setup at every
 * length and with every result length, each label and value column, what LEFT and RIGHT do to
 * each row (the clock, mode and count clamp, a nibble wraps and every byte is reachable), the
 * error texts and the confirmation text. No service call, no UI call.
 */
#include <stdio.h>
#include <string.h>
#include "spiapp.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

#define ROW(kind, idx) (&(spiview_row_t){(kind), (idx)})

static void test_the_default_is_the_jedec_id_at_1_mhz_mode_0(void)
{
    spiapp_cfg_t c;
    memset(&c, 0xAA, sizeof c);
    spiview_cfg_reset(&c);
    TEST_ASSERT_EQUAL_UINT32(1000000u, spiview_hz(&c));
    TEST_ASSERT_EQUAL_UINT8(0, c.mode);
    TEST_ASSERT_EQUAL_UINT8(4, c.count);
    TEST_ASSERT_TRUE(c.low_nibble);
    const uint8_t want[4] = {0x9f, 0, 0, 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, c.data, 4);
    for (size_t i = 4; i < SPIAPP_BYTES_MAX; i++) {
        TEST_ASSERT_EQUAL_HEX8(0, c.data[i]);
    }
}

static void test_the_presets_load_their_bytes_and_keep_the_clock_and_mode(void)
{
    spiapp_cfg_t c;
    spiview_cfg_reset(&c);
    c.clock = 5;
    c.mode = 3;
    c.data[9] = 0x77;
    TEST_ASSERT_TRUE(spiview_preset(&c, 1)); /* read status: 05, then one byte */
    TEST_ASSERT_EQUAL_UINT8(2, c.count);
    TEST_ASSERT_EQUAL_HEX8(0x05, c.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0, c.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0, c.data[9]); /* nothing of the earlier bytes is left behind */
    TEST_ASSERT_EQUAL_UINT32(4000000u, spiview_hz(&c));
    TEST_ASSERT_EQUAL_UINT8(3, c.mode);
    TEST_ASSERT_TRUE(spiview_preset(&c, 2)); /* dummy read: four zeros */
    TEST_ASSERT_EQUAL_UINT8(4, c.count);
    for (size_t i = 0; i < SPIAPP_BYTES_MAX; i++) {
        TEST_ASSERT_EQUAL_HEX8(0, c.data[i]);
    }
    TEST_ASSERT_TRUE(spiview_preset(&c, 0));
    TEST_ASSERT_EQUAL_HEX8(0x9f, c.data[0]);
    TEST_ASSERT_EQUAL_UINT8(4, c.count);
    spiapp_cfg_t before = c;
    TEST_ASSERT_FALSE(spiview_preset(&c, 3));
    TEST_ASSERT_EQUAL_MEMORY(&before, &c, sizeof c);
}

static void test_the_rows_of_the_default_setup(void)
{
    spiapp_cfg_t c;
    spiview_cfg_reset(&c);
    spiview_row_t r[SPIAPP_ROWS_MAX];
    TEST_ASSERT_EQUAL_UINT(13, spiview_rows(&c, 0, r));
    const spiview_kind_t want[13] = {SPIROW_CLOCK,  SPIROW_MODE,  SPIROW_PRESET, SPIROW_PRESET,
                                     SPIROW_PRESET, SPIROW_COUNT, SPIROW_NIBBLE, SPIROW_BYTE,
                                     SPIROW_BYTE,   SPIROW_BYTE,  SPIROW_BYTE,   SPIROW_SEND,
                                     SPIROW_NOTE};
    for (size_t i = 0; i < 13; i++) {
        TEST_ASSERT_EQUAL_INT(want[i], r[i].kind);
    }
    TEST_ASSERT_EQUAL_UINT8(0, r[2].idx);
    TEST_ASSERT_EQUAL_UINT8(1, r[3].idx);
    TEST_ASSERT_EQUAL_UINT8(2, r[4].idx);
    TEST_ASSERT_EQUAL_UINT8(3, r[10].idx);
    TEST_ASSERT_EQUAL_INT(SPIROW_SEND, r[11].kind);
}

static void test_result_rows_follow_the_note_a_hex_and_a_text_row_per_eight(void)
{
    spiapp_cfg_t c;
    spiview_cfg_reset(&c);
    spiview_row_t r[SPIAPP_ROWS_MAX];
    TEST_ASSERT_EQUAL_UINT(13 + 2, spiview_rows(&c, 1, r));
    TEST_ASSERT_EQUAL_INT(SPIROW_HEX, r[13].kind);
    TEST_ASSERT_EQUAL_INT(SPIROW_TEXT, r[14].kind);
    TEST_ASSERT_EQUAL_UINT(13 + 2, spiview_rows(&c, 8, r));
    TEST_ASSERT_EQUAL_UINT(13 + 4, spiview_rows(&c, 9, r));
    TEST_ASSERT_EQUAL_UINT8(1, r[15].idx);
    TEST_ASSERT_EQUAL_UINT8(1, r[16].idx);
    /* the longest: 32 bytes of editor and 32 bytes of result is the bound */
    c.count = 32;
    TEST_ASSERT_EQUAL_UINT(SPIAPP_ROWS_MAX, spiview_rows(&c, 32, r));
    TEST_ASSERT_EQUAL_UINT(SPIAPP_ROWS_MAX, spiview_rows(&c, 33, r)); /* never more than four */
    TEST_ASSERT_EQUAL_UINT(SPIAPP_ROWS_MAX, spiview_rows(&c, 500, r));
    TEST_ASSERT_EQUAL_UINT(SPIAPP_ROWS_MAX - 8u, spiview_rows(&c, 0, r));
    c.count = 1;
    TEST_ASSERT_EQUAL_UINT(2 + 3 + 2 + 1 + 2, spiview_rows(&c, 0, r));
    c.count = 255; /* a count past the limit never writes past the editor */
    TEST_ASSERT_EQUAL_UINT(SPIAPP_ROWS_MAX - 8u, spiview_rows(&c, 0, r));
}

static void test_every_label_and_the_note(void)
{
    TEST_ASSERT_EQUAL_STRING("Clock", spiview_label(&(spiview_row_t){SPIROW_CLOCK, 0}));
    TEST_ASSERT_EQUAL_STRING("Mode", spiview_label(&(spiview_row_t){SPIROW_MODE, 0}));
    TEST_ASSERT_EQUAL_STRING("JEDEC ID", spiview_label(&(spiview_row_t){SPIROW_PRESET, 0}));
    TEST_ASSERT_EQUAL_STRING("Read status", spiview_label(&(spiview_row_t){SPIROW_PRESET, 1}));
    TEST_ASSERT_EQUAL_STRING("Dummy read", spiview_label(&(spiview_row_t){SPIROW_PRESET, 2}));
    TEST_ASSERT_EQUAL_STRING("Bytes", spiview_label(&(spiview_row_t){SPIROW_COUNT, 0}));
    TEST_ASSERT_EQUAL_STRING("Nibble", spiview_label(&(spiview_row_t){SPIROW_NIBBLE, 0}));
    TEST_ASSERT_EQUAL_STRING("Byte", spiview_label(&(spiview_row_t){SPIROW_BYTE, 0}));
    TEST_ASSERT_EQUAL_STRING("Byte", spiview_label(&(spiview_row_t){SPIROW_BYTE, 31}));
    TEST_ASSERT_EQUAL_STRING("Send", spiview_label(&(spiview_row_t){SPIROW_SEND, 0}));
    TEST_ASSERT_EQUAL_STRING("MISO 0-7", spiview_label(&(spiview_row_t){SPIROW_HEX, 0}));
    TEST_ASSERT_EQUAL_STRING("MISO 24-31", spiview_label(&(spiview_row_t){SPIROW_HEX, 3}));
    TEST_ASSERT_EQUAL_STRING("Text 8-15", spiview_label(&(spiview_row_t){SPIROW_TEXT, 1}));
    TEST_ASSERT_EQUAL_STRING("", spiview_label(&(spiview_row_t){SPIROW_NOTE, 0}));
    TEST_ASSERT_EQUAL_STRING("Own hardware only", spiview_note());
    /* every label is a constant that is never NULL, even for an index past the table */
    for (int k = SPIROW_CLOCK; k <= SPIROW_TEXT; k++) {
        for (unsigned i = 0; i < 40; i++) {
            TEST_ASSERT_NOT_NULL(spiview_label(&(spiview_row_t){(spiview_kind_t)k, (uint8_t)i}));
        }
    }
    TEST_ASSERT_EQUAL_STRING("", spiview_label(&(spiview_row_t){(spiview_kind_t)99, 0}));
}

static void test_every_value_column(void)
{
    spiapp_cfg_t c;
    spiview_cfg_reset(&c);
    c.data[1] = 0xab;
    char buf[40];
    const uint8_t res[10] = {0x00, 0xef, 0x40, 0x18, 'A', 'z', 0x7f, 0x1f, 0xff, 'Q'};
    TEST_ASSERT_EQUAL_STRING("1 MHz",
                             spiview_value(ROW(SPIROW_CLOCK, 0), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING("0", spiview_value(ROW(SPIROW_MODE, 0), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING("9f + 3",
                             spiview_value(ROW(SPIROW_PRESET, 0), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING("05 + 1",
                             spiview_value(ROW(SPIROW_PRESET, 1), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING("4 x 00",
                             spiview_value(ROW(SPIROW_PRESET, 2), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING("4", spiview_value(ROW(SPIROW_COUNT, 0), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING("low", spiview_value(ROW(SPIROW_NIBBLE, 0), &c, res, 10, "", buf, 40));
    c.low_nibble = false;
    TEST_ASSERT_EQUAL_STRING("high",
                             spiview_value(ROW(SPIROW_NIBBLE, 0), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING("0: 0x9f",
                             spiview_value(ROW(SPIROW_BYTE, 0), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING("1: 0xab",
                             spiview_value(ROW(SPIROW_BYTE, 1), &c, res, 10, "", buf, 40));
    TEST_ASSERT_NULL(spiview_value(ROW(SPIROW_SEND, 0), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING("slot busy",
                             spiview_value(ROW(SPIROW_SEND, 0), &c, res, 10, "slot busy", buf, 40));
    TEST_ASSERT_NULL(spiview_value(ROW(SPIROW_NOTE, 0), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING("00 ef 40 18 41 7a 7f 1f",
                             spiview_value(ROW(SPIROW_HEX, 0), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING("ff 51", spiview_value(ROW(SPIROW_HEX, 1), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING("..@.Az..",
                             spiview_value(ROW(SPIROW_TEXT, 0), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING(".Q", spiview_value(ROW(SPIROW_TEXT, 1), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING("", spiview_value(ROW(SPIROW_HEX, 2), &c, res, 10, "", buf, 40));
    TEST_ASSERT_EQUAL_STRING("", spiview_value(ROW(SPIROW_TEXT, 2), &c, res, 10, "", buf, 40));
    /* a buffer that is too small cuts the text and still ends it */
    char tiny[4];
    TEST_ASSERT_EQUAL_STRING("..@", spiview_value(ROW(SPIROW_TEXT, 0), &c, res, 10, "", tiny, 4));
    char none[1] = {'x'};
    (void)spiview_value(ROW(SPIROW_TEXT, 0), &c, res, 10, "", none, 0);
    TEST_ASSERT_EQUAL_CHAR('x', none[0]); /* a zero-size buffer is never written */
}

static void test_the_clock_mode_and_count_stop_at_their_ends(void)
{
    spiapp_cfg_t c;
    spiview_cfg_reset(&c);
    c.clock = 0;
    TEST_ASSERT_FALSE(spiview_adjust(&c, ROW(SPIROW_CLOCK, 0), -1));
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(spiview_adjust(&c, ROW(SPIROW_CLOCK, 0), 1));
    }
    TEST_ASSERT_EQUAL_UINT32(4000000u, spiview_hz(&c));
    TEST_ASSERT_FALSE(spiview_adjust(&c, ROW(SPIROW_CLOCK, 0), 1));
    TEST_ASSERT_TRUE(spiview_adjust(&c, ROW(SPIROW_CLOCK, 0), -1));
    TEST_ASSERT_EQUAL_UINT32(2000000u, spiview_hz(&c));
    c.mode = 0;
    TEST_ASSERT_FALSE(spiview_adjust(&c, ROW(SPIROW_MODE, 0), -1));
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(spiview_adjust(&c, ROW(SPIROW_MODE, 0), 1));
    }
    TEST_ASSERT_EQUAL_UINT8(3, c.mode);
    TEST_ASSERT_FALSE(spiview_adjust(&c, ROW(SPIROW_MODE, 0), 1));
    c.count = 1;
    TEST_ASSERT_FALSE(spiview_adjust(&c, ROW(SPIROW_COUNT, 0), -1));
    for (int i = 0; i < 31; i++) {
        TEST_ASSERT_TRUE(spiview_adjust(&c, ROW(SPIROW_COUNT, 0), 1));
    }
    TEST_ASSERT_EQUAL_UINT8(32, c.count);
    TEST_ASSERT_FALSE(spiview_adjust(&c, ROW(SPIROW_COUNT, 0), 1));
    TEST_ASSERT_TRUE(spiview_adjust(&c, ROW(SPIROW_COUNT, 0), -1));
    TEST_ASSERT_EQUAL_UINT8(31, c.count);
}

static void test_a_nibble_wraps_inside_itself_and_every_byte_is_reachable(void)
{
    spiapp_cfg_t c;
    spiview_cfg_reset(&c);
    c.data[5] = 0xAF;
    TEST_ASSERT_TRUE(spiview_adjust(&c, ROW(SPIROW_BYTE, 5), 1)); /* low nibble F -> 0 */
    TEST_ASSERT_EQUAL_HEX8(0xA0, c.data[5]);
    TEST_ASSERT_TRUE(spiview_adjust(&c, ROW(SPIROW_BYTE, 5), -1)); /* 0 -> F */
    TEST_ASSERT_EQUAL_HEX8(0xAF, c.data[5]);
    TEST_ASSERT_TRUE(spiview_adjust(&c, ROW(SPIROW_NIBBLE, 0), 1));
    TEST_ASSERT_FALSE(c.low_nibble);
    TEST_ASSERT_TRUE(spiview_adjust(&c, ROW(SPIROW_BYTE, 5), 1)); /* high nibble A -> B */
    TEST_ASSERT_EQUAL_HEX8(0xBF, c.data[5]);
    TEST_ASSERT_TRUE(spiview_adjust(&c, ROW(SPIROW_NIBBLE, 0), -1)); /* either way toggles it */
    TEST_ASSERT_TRUE(c.low_nibble);
    /* all 256 values from zero: high steps then low steps */
    bool seen[256] = {false};
    for (unsigned hi = 0; hi < 16; hi++) {
        c.data[0] = 0;
        c.low_nibble = false;
        for (unsigned i = 0; i < hi; i++) {
            (void)spiview_adjust(&c, ROW(SPIROW_BYTE, 0), 1);
        }
        c.low_nibble = true;
        for (unsigned lo = 0; lo < 16; lo++) {
            seen[c.data[0]] = true;
            (void)spiview_adjust(&c, ROW(SPIROW_BYTE, 0), 1);
        }
    }
    for (unsigned v = 0; v < 256; v++) {
        TEST_ASSERT_TRUE(seen[v]);
    }
}

static void test_actions_notes_and_results_do_not_adjust(void)
{
    spiapp_cfg_t c;
    spiview_cfg_reset(&c);
    spiapp_cfg_t before = c;
    const spiview_kind_t none[] = {SPIROW_PRESET, SPIROW_SEND, SPIROW_NOTE, SPIROW_HEX,
                                   SPIROW_TEXT};
    for (size_t i = 0; i < sizeof none / sizeof none[0]; i++) {
        TEST_ASSERT_FALSE(spiview_adjust(&c, ROW(none[i], 0), 1));
        TEST_ASSERT_FALSE(spiview_adjust(&c, ROW(none[i], 0), -1));
    }
    TEST_ASSERT_FALSE(spiview_adjust(&c, ROW((spiview_kind_t)99, 0), 1));
    TEST_ASSERT_FALSE(spiview_adjust(&c, ROW(SPIROW_CLOCK, 0), 0)); /* no direction */
    TEST_ASSERT_FALSE(spiview_adjust(&c, ROW(SPIROW_BYTE, 0), 0));
    TEST_ASSERT_EQUAL_MEMORY(&before, &c, sizeof c);
}

static void test_error_texts_and_the_slot_column(void)
{
    TEST_ASSERT_EQUAL_STRING("slot busy", spiview_error_text(STARK_ERR_BUSY));
    TEST_ASSERT_EQUAL_STRING("refused", spiview_error_text(STARK_ERR_NOT_SUPPORTED));
    TEST_ASSERT_EQUAL_STRING("no module", spiview_error_text(STARK_ERR_NOT_FOUND));
    TEST_ASSERT_EQUAL_STRING("bad value", spiview_error_text(STARK_ERR_INVALID_ARG));
    TEST_ASSERT_EQUAL_STRING("not confirmed", spiview_error_text(STARK_ERR_STATE));
    TEST_ASSERT_EQUAL_STRING("bus error", spiview_error_text(STARK_ERR_IO));
    TEST_ASSERT_EQUAL_STRING("bus error", spiview_error_text(STARK_ERR_TIMEOUT));
    stark_lab_spi_slot_t s = {.slot = 0, .eligible = true};
    snprintf(s.name, sizeof s.name, "SPI breakout");
    snprintf(s.why, sizeof s.why, "ignored");
    TEST_ASSERT_EQUAL_STRING("SPI breakout", spiview_slot_value(&s));
    s.eligible = false;
    snprintf(s.why, sizeof s.why, "owned by Sub-GHz");
    TEST_ASSERT_EQUAL_STRING("owned by Sub-GHz", spiview_slot_value(&s));
}

static void test_the_confirmation_names_slot_clock_mode_and_bytes(void)
{
    spiapp_cfg_t c;
    spiview_cfg_reset(&c);
    char msg[160];
    spiview_message(0, &c, msg, sizeof msg);
    TEST_ASSERT_EQUAL_STRING("Slot 0, 1 MHz, mode 0: 9f 00 00 00. Own hardware only.", msg);
    c.clock = 0;
    c.mode = 3;
    c.count = 32;
    for (size_t i = 0; i < SPIAPP_BYTES_MAX; i++) {
        c.data[i] = (uint8_t)(0xE0u + i);
    }
    spiview_message(0, &c, msg, sizeof msg);
    TEST_ASSERT_TRUE(strlen(msg) <= 159u); /* it fits the dialog's limit */
    TEST_ASSERT_NOT_NULL(strstr(msg, "100 kHz, mode 3: e0 e1 "));
    TEST_ASSERT_NOT_NULL(strstr(msg, " ff. Own hardware only."));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_default_is_the_jedec_id_at_1_mhz_mode_0);
    RUN_TEST(test_the_presets_load_their_bytes_and_keep_the_clock_and_mode);
    RUN_TEST(test_the_rows_of_the_default_setup);
    RUN_TEST(test_result_rows_follow_the_note_a_hex_and_a_text_row_per_eight);
    RUN_TEST(test_every_label_and_the_note);
    RUN_TEST(test_every_value_column);
    RUN_TEST(test_the_clock_mode_and_count_stop_at_their_ends);
    RUN_TEST(test_a_nibble_wraps_inside_itself_and_every_byte_is_reachable);
    RUN_TEST(test_actions_notes_and_results_do_not_adjust);
    RUN_TEST(test_error_texts_and_the_slot_column);
    RUN_TEST(test_the_confirmation_names_slot_clock_mode_and_bytes);
    return UNITY_END();
}
