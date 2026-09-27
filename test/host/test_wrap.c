/*
 * test_wrap.c — text wrapping (TASKS.md STARK-0102): gfx_text_line(),
 * gfx_text_lines(), gfx_text_box().
 */
#include <stdio.h>
#include <string.h>
#include "gfx_font.h"
#include "ppm_dump.h"
#include "unity.h"

#define SW    96
#define SH    64
#define CLEAR 0x0000u
#define FG    0xFFFFu

static uint16_t s_px[SW * SH];
static gfx_surface_t s_surf;
static const gfx_font_t *const F = &gfx_font_mono16; /* 8 px cells */

void setUp(void)
{
    for (int i = 0; i < SW * SH; i++) {
        s_px[i] = CLEAR;
    }
    gfx_surface_init(&s_surf, s_px, SW, SH, 0, 0);
}

void tearDown(void)
{
    if (Unity.CurrentTestFailed) {
        char path[128];
        snprintf(path, sizeof path, "%s.ppm", Unity.CurrentTestName);
        (void)ppm_dump(path, s_px, SW, SH);
    }
}

/* Splits utf8 at max_w into "line|line|…" for compact assertions. */
static const char *split(const char *utf8, int16_t max_w)
{
    static char out[256];
    size_t o = 0;
    int lines = 0;
    const char *p = utf8;
    while (*p != '\0') {
        const char *next;
        size_t len = gfx_text_line(F, p, max_w, &next);
        TEST_ASSERT_TRUE_MESSAGE(next > p, "no progress");
        if (lines++ > 0) {
            out[o++] = '|';
        }
        memcpy(&out[o], p, len);
        o += len;
        p = next;
    }
    out[o] = '\0';
    return out;
}

static void test_fits_on_one_line(void)
{
    TEST_ASSERT_EQUAL_STRING("hello", split("hello", 40)); /* exactly 5 cells */
    TEST_ASSERT_EQUAL_INT16(1, gfx_text_lines(F, "hello", 40));
}

static void test_one_cell_overflow_breaks_at_the_space(void)
{
    TEST_ASSERT_EQUAL_STRING("hello|world", split("hello world", 40));
    TEST_ASSERT_EQUAL_STRING("ab cd|ef", split("ab cd ef", 40));
}

static void test_word_wider_than_line_is_hard_broken(void)
{
    TEST_ASSERT_EQUAL_STRING("abcd|efgh|ij", split("abcdefghij", 32));
    TEST_ASSERT_EQUAL_STRING("a|abcd|efg", split("a abcdefg", 32));
}

static void test_runs_of_spaces_and_edges(void)
{
    TEST_ASSERT_EQUAL_STRING("ab|cd", split("ab    cd", 24));
    TEST_ASSERT_EQUAL_STRING("  ab|cd", split("  ab cd", 32)); /* leading kept */
    TEST_ASSERT_EQUAL_STRING("ab", split("ab   ", 16));        /* trailing dropped */
    TEST_ASSERT_EQUAL_INT16(1, gfx_text_lines(F, "ab   ", 16));
}

static void test_newline_breaks_and_is_consumed(void)
{
    TEST_ASSERT_EQUAL_STRING("ab|cd", split("ab\ncd", 80));
    TEST_ASSERT_EQUAL_STRING("|ab", split("\nab", 80));
    TEST_ASSERT_EQUAL_STRING("ab||cd", split("ab\n\ncd", 80));
    TEST_ASSERT_EQUAL_INT16(1, gfx_text_lines(F, "ab\n", 80));
}

static void test_multibyte_never_split(void)
{
    /* "ğüzel" is 5 code points, 7 bytes: 3 cells per line. */
    TEST_ASSERT_EQUAL_STRING("ğüz|el", split("ğüzel", 24));
    const char *next;
    size_t len = gfx_text_line(F, "şş", 8, &next);
    TEST_ASSERT_EQUAL_size_t(2, len); /* one 2-byte code point */
    TEST_ASSERT_EQUAL_STRING("ş", next);
}

static void test_malformed_byte_is_one_cell(void)
{
    const char *next;
    TEST_ASSERT_EQUAL_size_t(2, gfx_text_line(F,
                                              "\xFF\xFE"
                                              "ab",
                                              16, &next));
    TEST_ASSERT_EQUAL_STRING("ab", next);
}

static void test_width_below_one_cell_still_progresses(void)
{
    TEST_ASSERT_EQUAL_STRING("a|b|c", split("abc", 3));
    TEST_ASSERT_EQUAL_STRING("a|b", split("a b", 0));
    TEST_ASSERT_EQUAL_INT16(3, gfx_text_lines(F, "abc", -5));
}

static void test_empty_and_null(void)
{
    const char *next = (const char *)1;
    TEST_ASSERT_EQUAL_size_t(0, gfx_text_line(F, "", 80, &next));
    TEST_ASSERT_EQUAL_STRING("", next);
    TEST_ASSERT_EQUAL_size_t(0, gfx_text_line(NULL, "ab", 80, &next));
    TEST_ASSERT_EQUAL_size_t(0, gfx_text_line(F, NULL, 80, &next));
    TEST_ASSERT_NULL(next);
    TEST_ASSERT_EQUAL_size_t(2, gfx_text_line(F, "ab", 80, NULL)); /* next optional */
    TEST_ASSERT_EQUAL_INT16(0, gfx_text_lines(F, "", 80));
    TEST_ASSERT_EQUAL_INT16(0, gfx_text_lines(NULL, "ab", 80));
    TEST_ASSERT_EQUAL_INT16(
        0, gfx_text_box(&s_surf, NULL, (gfx_rect_t){0, 0, 8, 8}, 0, "a", FG, CLEAR, true));
    TEST_ASSERT_EQUAL_INT16(
        0, gfx_text_box(&s_surf, F, (gfx_rect_t){0, 0, 0, 8}, 0, "a", FG, CLEAR, true));
    TEST_ASSERT_EQUAL_INT16(
        0, gfx_text_box(NULL, F, (gfx_rect_t){0, 0, 8, 8}, 0, "a", FG, CLEAR, true));
}

static int rows_with_ink(int y0, int y1)
{
    int rows = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = 0; x < SW; x++) {
            if (s_px[y * SW + x] == FG) {
                rows++;
                break;
            }
        }
    }
    return rows;
}

static void test_box_draws_what_lines_counts(void)
{
    const char *msg = "hello world foo";
    TEST_ASSERT_EQUAL_INT16(3, gfx_text_lines(F, msg, 48));
    TEST_ASSERT_EQUAL_INT16(
        3, gfx_text_box(&s_surf, F, (gfx_rect_t){0, 0, 48, 60}, 2, msg, FG, CLEAR, true));
    /* each 16 px cell sits 18 px after the previous one */
    TEST_ASSERT_TRUE(rows_with_ink(0, 16) > 0);
    TEST_ASSERT_TRUE(rows_with_ink(18, 34) > 0);
    TEST_ASSERT_TRUE(rows_with_ink(36, 52) > 0);
    TEST_ASSERT_EQUAL_INT(0, rows_with_ink(52, SH));
}

static void test_box_stops_before_the_bottom_and_clips_right(void)
{
    /* Two 16 px lines fit a 40 px box; the third would cross its bottom. */
    TEST_ASSERT_EQUAL_INT16(
        2, gfx_text_box(&s_surf, F, (gfx_rect_t){0, 0, 24, 40}, 0, "aaa bbb ccc", FG, CLEAR, true));
    TEST_ASSERT_EQUAL_INT(0, rows_with_ink(32, SH));
    /* A box narrower than one cell still draws a code point per line, but
     * clipped to the box: nothing right of x = 4. */
    setUp();
    TEST_ASSERT_EQUAL_INT16(
        1, gfx_text_box(&s_surf, F, (gfx_rect_t){0, 0, 4, 16}, 0, "W", FG, CLEAR, true));
    for (int y = 0; y < SH; y++) {
        for (int x = 4; x < SW; x++) {
            TEST_ASSERT_EQUAL_HEX16(CLEAR, s_px[y * SW + x]);
        }
    }
    /* the surface clip is restored afterwards */
    TEST_ASSERT_EQUAL_INT16(SW, s_surf.clip.w);
    TEST_ASSERT_EQUAL_INT16(SH, s_surf.clip.h);
}

static void test_box_respects_an_outer_clip(void)
{
    s_surf.clip = (gfx_rect_t){0, 0, 16, 16};
    TEST_ASSERT_EQUAL_INT16(
        1, gfx_text_box(&s_surf, F, (gfx_rect_t){0, 0, 64, 16}, 0, "abcdefgh", FG, CLEAR, true));
    for (int y = 0; y < SH; y++) {
        for (int x = 16; x < SW; x++) {
            TEST_ASSERT_EQUAL_HEX16(CLEAR, s_px[y * SW + x]);
        }
    }
    s_surf.clip = (gfx_rect_t){100, 100, 5, 5}; /* disjoint: draws nothing, counts lines */
    setUp();
    s_surf.clip = (gfx_rect_t){100, 100, 5, 5};
    (void)gfx_text_box(&s_surf, F, (gfx_rect_t){0, 0, 64, 16}, 0, "ab", FG, CLEAR, true);
    TEST_ASSERT_EQUAL_INT(0, rows_with_ink(0, SH));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fits_on_one_line);
    RUN_TEST(test_one_cell_overflow_breaks_at_the_space);
    RUN_TEST(test_word_wider_than_line_is_hard_broken);
    RUN_TEST(test_runs_of_spaces_and_edges);
    RUN_TEST(test_newline_breaks_and_is_consumed);
    RUN_TEST(test_multibyte_never_split);
    RUN_TEST(test_malformed_byte_is_one_cell);
    RUN_TEST(test_width_below_one_cell_still_progresses);
    RUN_TEST(test_empty_and_null);
    RUN_TEST(test_box_draws_what_lines_counts);
    RUN_TEST(test_box_stops_before_the_bottom_and_clips_right);
    RUN_TEST(test_box_respects_an_outer_clip);
    return UNITY_END();
}
