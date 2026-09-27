/*
 * test_text.c — host unit tests for stark_gfx text (STARK-0014,
 * TESTING.md §2 "text"): width vs rendered extent, UTF-8 decoding, the
 * fallback glyph, and clipping mid-glyph; Latin-1/Turkish coverage and the
 * 6x10 font (STARK-0102).
 */
#include <stdio.h>
#include <string.h>
#include "gfx_font.h"
#include "ppm_dump.h"
#include "unity.h"

#define SW     320
#define SH     16
#define CLEAR  0x0000u
#define FG     0xFFFFu
#define BGTEXT 0x0841u /* opaque text background: distinct from CLEAR */

static uint16_t s_px[SW * SH];
static gfx_surface_t s_surf;
static const gfx_font_t *const F = &gfx_font_mono16;

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

/* Columns [first, last] that were painted at all; -1/-1 if none. */
static void painted_extent(int *first, int *last)
{
    *first = -1;
    *last = -1;
    for (int x = 0; x < SW; x++) {
        for (int y = 0; y < SH; y++) {
            if (s_px[y * SW + x] != CLEAR) {
                if (*first < 0) {
                    *first = x;
                }
                *last = x;
                break;
            }
        }
    }
}

/* ---- AC #1: width == rendered extent for 20 varied strings --------------- */

void test_width_matches_rendered_extent_for_20_strings(void)
{
    static const char *const strings[] = {
        "A",
        "Hello, world",
        " leading space",
        "trailing space ",
        "   ",
        "0123456789",
        "~!@#$%^&*()_+{}|:\"<>?",
        "Input Test",
        "STARK ONE v0",
        "\x7F",              /* DEL: unmapped -> fallback */
        "ş",                 /* 2-byte UTF-8 */
        "Çalışma Ağı",       /* mixed 1/2-byte */
        "€ 100",             /* 3-byte */
        "😀",                /* 4-byte */
        "a😀b€cşd",          /* mixed widths */
        "\t\ttabs",          /* control chars: fallback cells */
        "\xC3",              /* truncated 2-byte sequence */
        "\x80\xBF",          /* stray continuation bytes */
        "\xE2\x82",          /* truncated 3-byte sequence */
        "mid\xF0\x9F\x98xy", /* truncated 4-byte, then ASCII */
    };
    TEST_ASSERT_EQUAL_size_t(20, sizeof strings / sizeof strings[0]);
    for (size_t i = 0; i < 20; i++) {
        setUp();
        int16_t w = gfx_text_width(F, strings[i]);
        int16_t drawn = gfx_text(&s_surf, F, 0, 0, strings[i], FG, BGTEXT, false);
        int first, last;
        painted_extent(&first, &last);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(w, drawn, strings[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, first, strings[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(w - 1, last, strings[i]);
    }
}

/* ---- UTF-8 decode ----------------------------------------------------------- */

void test_multibyte_sequences_are_one_cell_each(void)
{
    TEST_ASSERT_EQUAL_INT16(8, gfx_text_width(F, "ş"));  /* C5 9F */
    TEST_ASSERT_EQUAL_INT16(8, gfx_text_width(F, "€"));  /* E2 82 AC */
    TEST_ASSERT_EQUAL_INT16(8, gfx_text_width(F, "😀")); /* F0 9F 98 80 */
    TEST_ASSERT_EQUAL_INT16(4 * 8, gfx_text_width(F, "aş€😀"));
}

void test_malformed_utf8_is_one_fallback_cell_per_byte(void)
{
    TEST_ASSERT_EQUAL_INT16(8, gfx_text_width(F, "\xC3"));              /* truncated */
    TEST_ASSERT_EQUAL_INT16(16, gfx_text_width(F, "\x80\xBF"));         /* stray continuations */
    TEST_ASSERT_EQUAL_INT16(16, gfx_text_width(F, "\xC0\xAF"));         /* overlong '/' */
    TEST_ASSERT_EQUAL_INT16(24, gfx_text_width(F, "\xED\xA0\x80"));     /* surrogate D800 */
    TEST_ASSERT_EQUAL_INT16(32, gfx_text_width(F, "\xF4\x90\x80\x80")); /* > U+10FFFF */
    TEST_ASSERT_EQUAL_INT16(8, gfx_text_width(F, "\xFF"));              /* invalid lead */
    TEST_ASSERT_EQUAL_INT16(3 * 8, gfx_text_width(F, "\xE2\x82Z"));     /* truncated, then 'Z' */
}

/* ---- fallback glyph ---------------------------------------------------------- */

static void render_cell(const char *s, uint16_t out[16 * 8])
{
    setUp();
    gfx_text(&s_surf, F, 0, 0, s, FG, BGTEXT, false);
    for (int y = 0; y < 16; y++) {
        memcpy(&out[y * 8], &s_px[y * SW], 8 * sizeof(uint16_t));
    }
}

void test_unmapped_code_points_render_the_fallback_glyph(void)
{
    uint16_t q[16 * 8], cell[16 * 8];
    render_cell("?", q);
    /* Outside U+0020..U+017F, or inside it without a source glyph (DEL and
     * the C1 controls hold the fallback bitmap), or malformed UTF-8. */
    static const char *const unmapped[] = {"€",        "😀",       "\x01", "\x7F",
                                           "\xC2\x80", "\xC2\x9F", "ƀ",    "\xFF"};
    for (size_t i = 0; i < sizeof unmapped / sizeof unmapped[0]; i++) {
        render_cell(unmapped[i], cell);
        TEST_ASSERT_EQUAL_HEX16_ARRAY(q, cell, 16 * 8);
    }
    render_cell("A", cell); /* and a mapped glyph is not the fallback */
    TEST_ASSERT_FALSE(memcmp(q, cell, sizeof q) == 0);
}

/* ---- Latin-1 / Turkish coverage (STARK-0102) ------------------------------- */

static void render_cell_font(const gfx_font_t *f, const char *s, uint16_t *out)
{
    setUp();
    gfx_text(&s_surf, f, 0, 0, s, FG, BGTEXT, false);
    for (int y = 0; y < f->h; y++) {
        memcpy(&out[y * f->w], &s_px[y * SW], (size_t)f->w * sizeof(uint16_t));
    }
}

static size_t ink(const uint16_t *cell, size_t n)
{
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        k += cell[i] == FG ? 1u : 0u;
    }
    return k;
}

static const char *const k_turkish[] = {"Ç", "ç", "Ğ", "ğ", "İ", "ı", "Ö", "ö", "Ş", "ş", "Ü", "ü"};
static const char *const k_latin1[] = {"ä", "é", "ñ", "ß", "Å", "ø", "¿", "©", "°", "½"};

static void assert_own_glyphs(const gfx_font_t *f, const char *const *s, size_t n)
{
    uint16_t q[16 * 8], cell[16 * 8];
    size_t cells = (size_t)f->w * f->h;
    render_cell_font(f, "?", q);
    for (size_t i = 0; i < n; i++) {
        render_cell_font(f, s[i], cell);
        TEST_ASSERT_FALSE_MESSAGE(memcmp(q, cell, cells * sizeof(uint16_t)) == 0, s[i]);
        TEST_ASSERT_TRUE_MESSAGE(ink(cell, cells) > 0, s[i]);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(f->w, gfx_text_width(f, s[i]), s[i]);
    }
}

void test_turkish_and_latin1_letters_have_their_own_glyphs(void)
{
    assert_own_glyphs(&gfx_font_mono16, k_turkish, sizeof k_turkish / sizeof k_turkish[0]);
    assert_own_glyphs(&gfx_font_mono16, k_latin1, sizeof k_latin1 / sizeof k_latin1[0]);
    assert_own_glyphs(&gfx_font_mono10, k_turkish, sizeof k_turkish / sizeof k_turkish[0]);
    assert_own_glyphs(&gfx_font_mono10, k_latin1, sizeof k_latin1 / sizeof k_latin1[0]);
}

void test_dotted_and_dotless_i_differ(void)
{
    uint16_t a[16 * 8], b[16 * 8];
    render_cell_font(F, "I", a);
    render_cell_font(F, "İ", b);
    TEST_ASSERT_FALSE(memcmp(a, b, sizeof a) == 0);
    render_cell_font(F, "i", a);
    render_cell_font(F, "ı", b);
    TEST_ASSERT_FALSE(memcmp(a, b, sizeof a) == 0);
}

void test_mono10_is_6x10_with_the_same_range_and_fallback(void)
{
    const gfx_font_t *m = &gfx_font_mono10;
    TEST_ASSERT_EQUAL_UINT8(6, m->w);
    TEST_ASSERT_EQUAL_UINT8(10, m->h);
    TEST_ASSERT_EQUAL_UINT32(0x20, m->first);
    TEST_ASSERT_EQUAL_UINT32(0x17F, m->last);
    TEST_ASSERT_EQUAL_UINT32('?', m->fallback);
    TEST_ASSERT_EQUAL_UINT32(gfx_font_mono16.first, m->first);
    TEST_ASSERT_EQUAL_UINT32(gfx_font_mono16.last, m->last);
    TEST_ASSERT_EQUAL_INT16(6 * 7, gfx_text_width(m, "Ğüzel ş")); /* 7 code points */

    uint16_t q[10 * 6], cell[10 * 6];
    render_cell_font(m, "?", q);
    render_cell_font(m, "€", cell);
    TEST_ASSERT_EQUAL_HEX16_ARRAY(q, cell, 10 * 6);
    render_cell_font(m, "\xC2\x85", cell); /* a C1 control: fallback bitmap */
    TEST_ASSERT_EQUAL_HEX16_ARRAY(q, cell, 10 * 6);
    /* a mono10 glyph paints nothing below row 10 */
    setUp();
    gfx_text(&s_surf, m, 0, 0, "Ş", FG, BGTEXT, false);
    for (int y = 10; y < SH; y++) {
        for (int x = 0; x < 6; x++) {
            TEST_ASSERT_EQUAL_HEX16(CLEAR, s_px[y * SW + x]);
        }
    }
}

void test_every_printable_ascii_glyph_has_ink_and_space_has_none(void)
{
    for (int c = 0x20; c <= 0x7E; c++) {
        char s[2] = {(char)c, '\0'};
        setUp();
        gfx_text(&s_surf, F, 0, 0, s, FG, CLEAR, true);
        size_t ink = 0;
        for (int i = 0; i < SW * SH; i++) {
            ink += s_px[i] == FG ? 1u : 0u;
        }
        if (c == ' ') {
            TEST_ASSERT_EQUAL_size_t(0, ink);
        } else {
            TEST_ASSERT_GREATER_THAN_size_t(0, ink);
        }
    }
}

void test_transparent_text_leaves_background(void)
{
    for (int i = 0; i < SW * SH; i++) {
        s_px[i] = 0x1234;
    }
    gfx_text(&s_surf, F, 0, 0, "A", FG, BGTEXT, true);
    for (int i = 0; i < SW * SH; i++) {
        TEST_ASSERT_TRUE(s_px[i] == 0x1234 || s_px[i] == FG);
    }
}

/* ---- AC #2: clipping mid-glyph ------------------------------------------------ */

void test_text_clipped_mid_glyph_at_the_left_edge(void)
{
    static uint16_t ref[SW * SH];
    gfx_text(&s_surf, F, 0, 0, "Wg", FG, BGTEXT, false);
    memcpy(ref, s_px, sizeof ref);
    setUp();
    int16_t w = gfx_text(&s_surf, F, -3, 0, "Wg", FG, BGTEXT, false);
    TEST_ASSERT_EQUAL_INT16(16, w); /* width is the string's, not the visible part */
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            uint16_t want = (x < 13) ? ref[y * SW + x + 3] : CLEAR;
            TEST_ASSERT_EQUAL_HEX16(want, s_px[y * SW + x]);
        }
    }
}

void test_text_clipped_mid_glyph_at_the_right_and_bottom_edges(void)
{
    static uint16_t ref[SW * SH];
    gfx_text(&s_surf, F, 0, 0, "Mq", FG, BGTEXT, false);
    memcpy(ref, s_px, sizeof ref);
    /* A small 12x10 surface: cuts through the second glyph and the descender. */
    static uint16_t small[12 * 10 + 16];
    for (size_t i = 0; i < sizeof small / sizeof small[0]; i++) {
        small[i] = 0xA5A5u;
    }
    gfx_surface_t s12;
    gfx_surface_init(&s12, small, 12, 10, 0, 0);
    for (int i = 0; i < 12 * 10; i++) {
        small[i] = CLEAR;
    }
    gfx_text(&s12, F, 0, 0, "Mq", FG, BGTEXT, false);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 12; x++) {
            TEST_ASSERT_EQUAL_HEX16(ref[y * SW + x], small[y * 12 + x]);
        }
    }
    for (int i = 12 * 10; i < 12 * 10 + 16; i++) {
        TEST_ASSERT_EQUAL_HEX16(0xA5A5u, small[i]); /* nothing beyond the buffer */
    }
}

void test_text_in_a_band_surface_uses_logical_coordinates(void)
{
    static uint16_t ref[SW * SH];
    gfx_text(&s_surf, F, 5, 0, "Band", FG, BGTEXT, false);
    memcpy(ref, s_px, sizeof ref);
    /* The lower half (rows 8..15) as its own band surface. */
    static uint16_t band[SW * 8];
    for (int i = 0; i < SW * 8; i++) {
        band[i] = CLEAR;
    }
    gfx_surface_t b;
    gfx_surface_init(&b, band, SW, 8, 0, 8);
    gfx_text(&b, F, 5, 0, "Band", FG, BGTEXT, false);
    TEST_ASSERT_EQUAL_HEX16_ARRAY(&ref[8 * SW], band, SW * 8);
}

/* ---- edges ----------------------------------------------------------------------- */

void test_null_font_or_string_and_empty_string(void)
{
    TEST_ASSERT_EQUAL_INT16(0, gfx_text_width(NULL, "abc"));
    TEST_ASSERT_EQUAL_INT16(0, gfx_text_width(F, NULL));
    TEST_ASSERT_EQUAL_INT16(0, gfx_text_width(F, ""));
    TEST_ASSERT_EQUAL_INT16(0, gfx_text(&s_surf, NULL, 0, 0, "abc", FG, BGTEXT, false));
    TEST_ASSERT_EQUAL_INT16(0, gfx_text(&s_surf, F, 0, 0, NULL, FG, BGTEXT, false));
    TEST_ASSERT_EQUAL_INT16(8, gfx_text(NULL, F, 0, 0, "a", FG, BGTEXT, false)); /* no surface */
    int first, last;
    painted_extent(&first, &last);
    TEST_ASSERT_EQUAL_INT(-1, first);
}

void test_width_saturates_and_drawing_stops_past_int16(void)
{
    static char big[5000];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, gfx_text_width(F, big)); /* 4999 * 8 > 32767 */
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, gfx_text(&s_surf, F, 32000, 0, big, FG, BGTEXT, false));
    int first, last;
    painted_extent(&first, &last);
    TEST_ASSERT_EQUAL_INT(-1, first); /* all of it lies right of the surface */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_width_matches_rendered_extent_for_20_strings);
    RUN_TEST(test_multibyte_sequences_are_one_cell_each);
    RUN_TEST(test_malformed_utf8_is_one_fallback_cell_per_byte);
    RUN_TEST(test_unmapped_code_points_render_the_fallback_glyph);
    RUN_TEST(test_turkish_and_latin1_letters_have_their_own_glyphs);
    RUN_TEST(test_dotted_and_dotless_i_differ);
    RUN_TEST(test_mono10_is_6x10_with_the_same_range_and_fallback);
    RUN_TEST(test_every_printable_ascii_glyph_has_ink_and_space_has_none);
    RUN_TEST(test_transparent_text_leaves_background);
    RUN_TEST(test_text_clipped_mid_glyph_at_the_left_edge);
    RUN_TEST(test_text_clipped_mid_glyph_at_the_right_and_bottom_edges);
    RUN_TEST(test_text_in_a_band_surface_uses_logical_coordinates);
    RUN_TEST(test_null_font_or_string_and_empty_string);
    RUN_TEST(test_width_saturates_and_drawing_stops_past_int16);
    return UNITY_END();
}
