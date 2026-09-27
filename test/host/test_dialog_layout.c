/*
 * test_dialog_layout.c — dialog geometry and UTF-8-safe copying
 * (TASKS.md STARK-0105).
 */
#include <string.h>
#include "gfx_font.h"
#include "ui_dialog_layout.h"
#include "unity.h"

static const gfx_rect_t CONTENT = {0, 16, 320, 224};
static ui_dialog_layout_t L;

void setUp(void)
{
    memset(&L, 0, sizeof L);
}

void tearDown(void)
{}

static void layout(const char *title, const char *msg, gfx_rect_t content)
{
    ui_dialog_layout(&gfx_font_mono16, &gfx_font_mono16, &gfx_font_mono10, title, msg, content, &L);
}

static void assert_inside(gfx_rect_t inner, gfx_rect_t outer)
{
    TEST_ASSERT_TRUE(inner.x >= outer.x && inner.y >= outer.y);
    TEST_ASSERT_TRUE(inner.x + inner.w <= outer.x + outer.w);
    TEST_ASSERT_TRUE(inner.y + inner.h <= outer.y + outer.h);
}

static void test_one_line_centred(void)
{
    layout("Confirm", "Run it?", CONTENT);
    TEST_ASSERT_EQUAL_INT16(UI_DIALOG_W, L.box.w);
    TEST_ASSERT_EQUAL_INT16((320 - UI_DIALOG_W) / 2, L.box.x);
    TEST_ASSERT_EQUAL_INT16(1, L.lines);
    /* border 2 + pad 6 each side; title 16, pad, 1 line 16, pad, hint 10 */
    TEST_ASSERT_EQUAL_INT16(16 + 16 + 6 + 16 + 6 + 10, L.box.h);
    TEST_ASSERT_EQUAL_INT16(16 + (224 - L.box.h) / 2, L.box.y);
    assert_inside(L.box, CONTENT);
    assert_inside(L.title, L.box);
    assert_inside(L.body, L.box);
    assert_inside(L.hint, L.box);
    TEST_ASSERT_EQUAL_INT16(L.box.x + 8, L.title.x);
    TEST_ASSERT_EQUAL_INT16(L.box.w - 16, L.title.w);
    TEST_ASSERT_EQUAL_size_t(7, L.title_bytes);
}

static void test_four_and_six_lines(void)
{
    /* 264 px inner width = 33 mono16 cells */
    const char *four = "line one\nline two\nline three\nline four";
    layout("T", four, CONTENT);
    TEST_ASSERT_EQUAL_INT16(4, L.lines);
    TEST_ASSERT_EQUAL_INT16(64, L.body.h);
    int16_t h4 = L.box.h;
    layout("T", "a\nb\nc\nd\ne\nf", CONTENT); /* six lines: clipped to four */
    TEST_ASSERT_EQUAL_INT16(4, L.lines);
    TEST_ASSERT_EQUAL_INT16(h4, L.box.h);
    assert_inside(L.box, CONTENT);
}

static void test_wrapping_counts_toward_lines(void)
{
    layout("T", "a long message that certainly needs more than one row to fit", CONTENT);
    TEST_ASSERT_EQUAL_INT16(2, L.lines);
}

static void test_empty_message(void)
{
    layout("Alert", "", CONTENT);
    TEST_ASSERT_EQUAL_INT16(0, L.lines);
    TEST_ASSERT_EQUAL_INT16(0, L.body.h);
    TEST_ASSERT_EQUAL_INT16(L.title.y + 16 + 6, L.hint.y); /* hint right after the title */
    TEST_ASSERT_EQUAL_INT16(16 + 16 + 6 + 10, L.box.h);
    layout(NULL, NULL, CONTENT); /* NULL reads as "" */
    TEST_ASSERT_EQUAL_INT16(0, L.lines);
    TEST_ASSERT_EQUAL_size_t(0, L.title_bytes);
}

static void test_long_title_truncates_on_a_code_point(void)
{
    char title[128] = "";
    for (int i = 0; i < 40; i++) {
        strcat(title, "Ş"); /* two bytes each */
    }
    layout(title, "x", CONTENT);
    TEST_ASSERT_EQUAL_size_t(33 * 2, L.title_bytes); /* 33 cells of 264 px */
    TEST_ASSERT_EQUAL_size_t(5, (layout("Short", "x", CONTENT), L.title_bytes));
}

static void test_narrow_content(void)
{
    layout("T", "m", (gfx_rect_t){10, 20, 200, 100});
    TEST_ASSERT_EQUAL_INT16(200, L.box.w);
    TEST_ASSERT_EQUAL_INT16(10, L.box.x);
    TEST_ASSERT_EQUAL_INT16(200 - 16, L.body.w);
}

static void test_utf8_copy(void)
{
    char d[8];
    TEST_ASSERT_EQUAL_size_t(3, ui_utf8_copy(d, sizeof d, "abc"));
    TEST_ASSERT_EQUAL_STRING("abc", d);
    TEST_ASSERT_EQUAL_size_t(7, ui_utf8_copy(d, sizeof d, "abcdefghij"));
    TEST_ASSERT_EQUAL_STRING("abcdefg", d);
    /* "abcdeŞ": the 2-byte Ş would straddle the cap (7 bytes available) */
    TEST_ASSERT_EQUAL_size_t(7, ui_utf8_copy(d, sizeof d, "abcdeŞx"));
    TEST_ASSERT_EQUAL_STRING("abcdeŞ", d);
    TEST_ASSERT_EQUAL_size_t(6, ui_utf8_copy(d, sizeof d, "abcdefŞ"));
    TEST_ASSERT_EQUAL_STRING("abcdef", d); /* cut through Ş: dropped whole */
    TEST_ASSERT_EQUAL_size_t(6, ui_utf8_copy(d, sizeof d, "abcdef€")); /* 3-byte € */
    TEST_ASSERT_EQUAL_STRING("abcdef", d);
    TEST_ASSERT_EQUAL_size_t(5, ui_utf8_copy(d, sizeof d, "abcde€"));
    TEST_ASSERT_EQUAL_STRING("abcde", d); /* € needs bytes 5..7: 7 is past the cap */
    TEST_ASSERT_EQUAL_size_t(0, ui_utf8_copy(d, sizeof d, NULL));
    TEST_ASSERT_EQUAL_STRING("", d);
    char one[1];
    TEST_ASSERT_EQUAL_size_t(0, ui_utf8_copy(one, sizeof one, "abc"));
    TEST_ASSERT_EQUAL_STRING("", one);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_one_line_centred);
    RUN_TEST(test_four_and_six_lines);
    RUN_TEST(test_wrapping_counts_toward_lines);
    RUN_TEST(test_empty_message);
    RUN_TEST(test_long_title_truncates_on_a_code_point);
    RUN_TEST(test_narrow_content);
    RUN_TEST(test_utf8_copy);
    return UNITY_END();
}
