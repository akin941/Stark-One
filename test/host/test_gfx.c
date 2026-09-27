/*
 * test_gfx.c — host unit tests for stark_gfx surfaces & primitives
 * (STARK-0013, TESTING.md §2 "gfx").
 *
 * Every surface lives between guard words, so a stray write outside the
 * buffer fails the test even when it does not crash. On failure the
 * surface under test is written to <test name>.ppm next to the test
 * binary (uploaded as a CI artifact).
 */
#include <stdio.h>
#include <string.h>
#include "ppm_dump.h"
#include "stark_gfx.h"
#include "unity.h"

#define W      16
#define H      8
#define GUARD  16
#define BG     0x0000u
#define INK    0xFFFFu
#define CANARY 0xA5A5u

static uint16_t s_mem[GUARD + W * H * 4 + GUARD]; /* room for a 16x32 screen */
static uint16_t *s_px = s_mem + GUARD;
static gfx_surface_t s_surf;
static const gfx_surface_t *s_dump; /* dumped by tearDown on failure */
static int s_dump_w, s_dump_h;
static size_t s_region; /* pixels in use; everything after them is guard */

static void guarded_reset(size_t n_pixels)
{
    s_region = n_pixels;
    for (size_t i = 0; i < sizeof s_mem / sizeof s_mem[0]; i++) {
        s_mem[i] = CANARY;
    }
    for (size_t i = 0; i < n_pixels; i++) {
        s_px[i] = BG;
    }
}

static void assert_guards(size_t n_pixels)
{
    for (size_t i = 0; i < GUARD; i++) {
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(CANARY, s_mem[i], "write before the buffer");
    }
    for (size_t i = GUARD + n_pixels; i < sizeof s_mem / sizeof s_mem[0]; i++) {
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(CANARY, s_mem[i], "write after the buffer");
    }
}

static uint16_t px(int x, int y)
{
    return s_px[y * W + x];
}

static size_t count_ink(void)
{
    size_t n = 0;
    for (int i = 0; i < W * H; i++) {
        n += s_px[i] == INK ? 1u : 0u;
    }
    return n;
}

/* Asserts exactly the pixels inside [x0,x1) x [y0,y1) are INK. */
static void assert_ink_exactly(int x0, int y0, int x1, int y1)
{
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            bool in = x >= x0 && x < x1 && y >= y0 && y < y1;
            TEST_ASSERT_EQUAL_HEX16(in ? INK : BG, px(x, y));
        }
    }
}

static gfx_rect_t rect(int x, int y, int w, int h)
{
    return (gfx_rect_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
}

void setUp(void)
{
    guarded_reset(W * H);
    gfx_surface_init(&s_surf, s_px, W, H, 0, 0);
    s_dump = &s_surf;
    s_dump_w = W;
    s_dump_h = H;
}

void tearDown(void)
{
    if (Unity.CurrentTestFailed && s_dump != NULL) {
        char path[128];
        snprintf(path, sizeof path, "%s.ppm", Unity.CurrentTestName);
        (void)ppm_dump(path, s_dump->pixels, s_dump_w, s_dump_h);
    }
    assert_guards(s_region);
}

/* ---- surface ------------------------------------------------------------ */

void test_surface_init_clips_to_its_own_logical_area(void)
{
    gfx_surface_t s;
    gfx_surface_init(&s, s_px, 10, 4, 3, 40);
    TEST_ASSERT_EQUAL_PTR(s_px, s.pixels);
    TEST_ASSERT_EQUAL_INT16(3, s.clip.x);
    TEST_ASSERT_EQUAL_INT16(40, s.clip.y);
    TEST_ASSERT_EQUAL_INT16(10, s.clip.w);
    TEST_ASSERT_EQUAL_INT16(4, s.clip.h);
    gfx_surface_init(NULL, s_px, 1, 1, 0, 0); /* must not crash */
}

void test_rgb565_macro(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, GFX_RGB565(255, 255, 255));
    TEST_ASSERT_EQUAL_HEX16(0xF800, GFX_RGB565(255, 0, 0));
    TEST_ASSERT_EQUAL_HEX16(0x07E0, GFX_RGB565(0, 255, 0));
    TEST_ASSERT_EQUAL_HEX16(0x001F, GFX_RGB565(0, 0, 255));
    TEST_ASSERT_EQUAL_HEX16(0x0000, GFX_RGB565(7, 3, 7)); /* below one step */
}

/* ---- fill and boundary pixels ------------------------------------------ */

void test_fill_paints_exactly_its_rectangle(void)
{
    gfx_fill(&s_surf, rect(3, 2, 5, 4), INK);
    assert_ink_exactly(3, 2, 8, 6);
}

void test_fill_covering_the_whole_buffer_and_more(void)
{
    gfx_fill(&s_surf, rect(-100, -100, 1000, 1000), INK);
    assert_ink_exactly(0, 0, W, H);
}

void test_fill_is_clipped_at_each_edge(void)
{
    gfx_fill(&s_surf, rect(-3, 2, 5, 2), INK); /* left */
    assert_ink_exactly(0, 2, 2, 4);
    setUp();
    gfx_fill(&s_surf, rect(W - 2, 2, 5, 2), INK); /* right */
    assert_ink_exactly(W - 2, 2, W, 4);
    setUp();
    gfx_fill(&s_surf, rect(4, -5, 3, 6), INK); /* top */
    assert_ink_exactly(4, 0, 7, 1);
    setUp();
    gfx_fill(&s_surf, rect(4, H - 1, 3, 9), INK); /* bottom */
    assert_ink_exactly(4, H - 1, 7, H);
}

void test_fully_outside_rectangles_write_nothing(void)
{
    const gfx_rect_t outside[] = {
        rect(-5, 0, 5, H),    /* ends exactly at the left edge */
        rect(W, 0, 4, H),     /* starts exactly at the right edge */
        rect(0, -3, W, 3),    /* above */
        rect(0, H, W, 2),     /* below */
        rect(-10, -10, 3, 3), /* diagonal */
        rect(30000, 30000, 2000, 2000),
        rect(-32768, -32768, 32767, 1),
    };
    for (size_t i = 0; i < sizeof outside / sizeof outside[0]; i++) {
        gfx_fill(&s_surf, outside[i], INK);
        gfx_rect(&s_surf, outside[i], INK);
    }
    TEST_ASSERT_EQUAL_size_t(0, count_ink());
}

void test_zero_and_negative_sizes_draw_nothing(void)
{
    gfx_fill(&s_surf, rect(2, 2, 0, 3), INK);
    gfx_fill(&s_surf, rect(2, 2, 3, 0), INK);
    gfx_fill(&s_surf, rect(5, 5, -3, -3), INK);
    gfx_rect(&s_surf, rect(5, 5, -2, 4), INK);
    gfx_hline(&s_surf, 2, 2, 0, INK);
    gfx_hline(&s_surf, 2, 2, -4, INK);
    gfx_vline(&s_surf, 2, 2, 0, INK);
    gfx_vline(&s_surf, 2, 2, -4, INK);
    TEST_ASSERT_EQUAL_size_t(0, count_ink());
}

void test_no_int16_overflow_near_the_limits(void)
{
    gfx_fill(&s_surf, rect(32000, 0, 32000, H), INK); /* x + w overflows int16 */
    TEST_ASSERT_EQUAL_size_t(0, count_ink());
    gfx_fill(&s_surf, rect(-30000, 1, 30010, 1), INK); /* ends at x = 10 */
    assert_ink_exactly(0, 1, 10, 2);
}

void test_null_surface_pixels_and_bits_are_ignored(void)
{
    static const uint8_t bits[1] = {0xFF};
    gfx_fill(NULL, rect(0, 0, 4, 4), INK);
    gfx_rect(NULL, rect(0, 0, 4, 4), INK);
    gfx_hline(NULL, 0, 0, 4, INK);
    gfx_vline(NULL, 0, 0, 4, INK);
    gfx_blit_1bpp(NULL, 0, 0, bits, 8, 1, INK, BG, false);
    gfx_blit_1bpp(&s_surf, 0, 0, NULL, 8, 1, INK, BG, false);
    gfx_surface_t no_pixels;
    gfx_surface_init(&no_pixels, NULL, W, H, 0, 0);
    gfx_fill(&no_pixels, rect(0, 0, 4, 4), INK);
    gfx_surface_t empty;
    gfx_surface_init(&empty, s_px, 0, H, 0, 0);
    gfx_fill(&empty, rect(0, 0, 4, 4), INK);
    TEST_ASSERT_EQUAL_size_t(0, count_ink());
}

/* ---- lines and outlines -------------------------------------------------- */

void test_hline_and_vline_boundary_pixels(void)
{
    gfx_hline(&s_surf, -2, H - 1, 5, INK); /* clipped on the left, bottom row */
    gfx_vline(&s_surf, W - 1, -4, 6, INK); /* clipped at the top, right column */
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            bool in = (y == H - 1 && x <= 2) || (x == W - 1 && y <= 1);
            TEST_ASSERT_EQUAL_HEX16(in ? INK : BG, px(x, y));
        }
    }
}

void test_rect_draws_only_the_outline(void)
{
    gfx_rect(&s_surf, rect(2, 1, 6, 4), INK);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            bool inside = x >= 2 && x < 8 && y >= 1 && y < 5;
            bool border = inside && (x == 2 || x == 7 || y == 1 || y == 4);
            TEST_ASSERT_EQUAL_HEX16(border ? INK : BG, px(x, y));
        }
    }
}

void test_degenerate_rects(void)
{
    gfx_rect(&s_surf, rect(1, 1, 1, 1), INK); /* single pixel */
    TEST_ASSERT_EQUAL_size_t(1, count_ink());
    setUp();
    gfx_rect(&s_surf, rect(3, 0, 1, 5), INK); /* 1 px wide: a vertical line */
    assert_ink_exactly(3, 0, 4, 5);
    setUp();
    gfx_rect(&s_surf, rect(0, 6, 7, 1), INK); /* 1 px tall */
    assert_ink_exactly(0, 6, 7, 7);
    setUp();
    gfx_rect(&s_surf, rect(4, 2, 2, 2), INK); /* 2x2: all border */
    assert_ink_exactly(4, 2, 6, 4);
}

void test_rect_clipped_by_the_edge_keeps_its_visible_sides(void)
{
    gfx_rect(&s_surf, rect(-2, -2, 6, 5), INK); /* only right and bottom sides visible */
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            bool in = (x == 3 && y <= 2) || (y == 2 && x <= 3);
            TEST_ASSERT_EQUAL_HEX16(in ? INK : BG, px(x, y));
        }
    }
}

void test_narrowed_clip_limits_every_primitive(void)
{
    static const uint8_t bits[2] = {0xFF, 0xFF};
    s_surf.clip = rect(4, 2, 5, 3);
    gfx_fill(&s_surf, rect(0, 0, W, H), INK);
    assert_ink_exactly(4, 2, 9, 5);
    setUp();
    s_surf.clip = rect(4, 2, 5, 3);
    gfx_hline(&s_surf, 0, 3, W, INK);
    gfx_vline(&s_surf, 6, 0, H, INK);
    gfx_blit_1bpp(&s_surf, 0, 4, bits, 16, 1, INK, BG, false);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            bool in = x >= 4 && x < 9 && y >= 2 && y < 5 && (y == 3 || x == 6 || y == 4);
            TEST_ASSERT_EQUAL_HEX16(in ? INK : BG, px(x, y));
        }
    }
    setUp();
    s_surf.clip = rect(4, 2, -1, 3); /* empty clip */
    gfx_fill(&s_surf, rect(0, 0, W, H), INK);
    TEST_ASSERT_EQUAL_size_t(0, count_ink());
}

/* ---- 1bpp blit ------------------------------------------------------------ */

/* 10x3, rows padded to 2 bytes, MSB first:
 *   row 0: X.X.X.X.X.
 *   row 1: XXXXX.....
 *   row 2: .........X   */
static const uint8_t k_glyph[] = {0xAA, 0x80, 0xF8, 0x00, 0x00, 0x40};

static bool glyph_bit(int gx, int gy)
{
    return ((k_glyph[gy * 2 + gx / 8] >> (7 - gx % 8)) & 1) != 0;
}

void test_blit_opaque_paints_fg_and_bg(void)
{
    guarded_reset(W * H);
    for (int i = 0; i < W * H; i++) {
        s_px[i] = 0x1234; /* neither fg nor bg */
    }
    gfx_blit_1bpp(&s_surf, 2, 3, k_glyph, 10, 3, INK, BG, false);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            bool in = x >= 2 && x < 12 && y >= 3 && y < 6;
            uint16_t want = !in ? 0x1234 : (glyph_bit(x - 2, y - 3) ? INK : BG);
            TEST_ASSERT_EQUAL_HEX16(want, px(x, y));
        }
    }
}

void test_blit_transparent_leaves_clear_bits_alone(void)
{
    for (int i = 0; i < W * H; i++) {
        s_px[i] = 0x1234;
    }
    gfx_blit_1bpp(&s_surf, 2, 3, k_glyph, 10, 3, INK, BG, true);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            bool in = x >= 2 && x < 12 && y >= 3 && y < 6;
            uint16_t want = (in && glyph_bit(x - 2, y - 3)) ? INK : 0x1234;
            TEST_ASSERT_EQUAL_HEX16(want, px(x, y));
        }
    }
}

void test_blit_clipped_at_left_and_top_uses_the_right_source_bits(void)
{
    gfx_blit_1bpp(&s_surf, -3, -1, k_glyph, 10, 3, INK, BG, false);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            bool in = x < 7 && y < 2;
            uint16_t want = !in ? BG : (glyph_bit(x + 3, y + 1) ? INK : BG);
            TEST_ASSERT_EQUAL_HEX16(want, px(x, y));
        }
    }
    TEST_ASSERT_EQUAL_HEX16(INK, px(1, 0)); /* glyph (4,1) */
    TEST_ASSERT_EQUAL_HEX16(INK, px(6, 1)); /* glyph (9,2) */
}

void test_blit_clipped_at_right_and_bottom(void)
{
    gfx_blit_1bpp(&s_surf, W - 4, H - 2, k_glyph, 10, 3, INK, BG, false);
    for (int y = H - 2; y < H; y++) {
        for (int x = W - 4; x < W; x++) {
            TEST_ASSERT_EQUAL_HEX16(glyph_bit(x - (W - 4), y - (H - 2)) ? INK : BG, px(x, y));
        }
    }
}

/* ---- origin translation: band rendering (AC #2) ---------------------------- */

#define SCR_H  (H * 4) /* a 16x32 logical screen */
#define BAND_H H       /* rendered as four 16x8 bands */

static void draw_scene(gfx_surface_t *s)
{
    gfx_fill(s, rect(2, 5, 4, 20), INK);                              /* spans bands 0..3 */
    gfx_hline(s, 0, 25, W, 0x0F0Fu);                                  /* band 3 only */
    gfx_rect(s, rect(9, 1, 6, 30), 0xF800u);                          /* crosses every band */
    gfx_vline(s, 7, 12, 4, 0x07E0u);                                  /* band 1 only */
    gfx_blit_1bpp(s, 5, 22, k_glyph, 10, 3, 0x001Fu, 0x3333u, false); /* bands 2..3 */
}

void test_same_logical_draw_lands_in_the_right_rows_of_every_band(void)
{
    static uint16_t ref[W * SCR_H];
    gfx_surface_t full;
    for (int i = 0; i < W * SCR_H; i++) {
        ref[i] = BG;
    }
    gfx_surface_init(&full, ref, W, SCR_H, 0, 0);
    draw_scene(&full);

    guarded_reset(W * SCR_H);
    for (int band = 0; band < SCR_H / BAND_H; band++) {
        uint16_t *band_px = s_px + band * W * BAND_H; /* consecutive, guarded */
        static gfx_surface_t b; /* static: tearDown may dump it after a failure */
        gfx_surface_init(&b, band_px, W, BAND_H, 0, (int16_t)(band * BAND_H));
        draw_scene(&b);
        s_dump = &b;
        s_dump_h = BAND_H;
        for (int y = 0; y < BAND_H; y++) {
            TEST_ASSERT_EQUAL_HEX16_ARRAY(&ref[(band * BAND_H + y) * W], &band_px[y * W], W);
        }
    }
    /* Explicitly (AC #2): band 0 row 5 and band 3 row 1 (logical 25). */
    TEST_ASSERT_EQUAL_HEX16(INK, s_px[5 * W + 2]);
    TEST_ASSERT_EQUAL_HEX16(0x0F0Fu, s_px[(3 * BAND_H + 1) * W + 0]);
    TEST_ASSERT_EQUAL_HEX16(BG, s_px[1 * W + 0]); /* the band-3 hline is not in band 0 */
    s_dump = NULL;
}

void test_draw_entirely_outside_a_band_is_rejected(void)
{
    gfx_surface_t band3;
    gfx_surface_init(&band3, s_px, W, BAND_H, 0, 3 * BAND_H);
    gfx_fill(&band3, rect(0, 0, W, 3 * BAND_H), INK); /* bands 0..2 only */
    TEST_ASSERT_EQUAL_size_t(0, count_ink());
    gfx_fill(&band3, rect(0, 3 * BAND_H - 1, W, 2), INK); /* last row of band 2 + first of 3 */
    assert_ink_exactly(0, 0, W, 1);
}

void test_origin_x_translates_horizontally(void)
{
    gfx_surface_t right_half;
    gfx_surface_init(&right_half, s_px, W, H, 100, 0); /* logical x 100..115 */
    gfx_fill(&right_half, rect(98, 0, 4, 1), INK);     /* logical 98..101 */
    assert_ink_exactly(0, 0, 2, 1);
}

/* ---- test support ---------------------------------------------------------- */

void test_ppm_dump_writes_a_p6_image(void)
{
    uint16_t img[2] = {0xF800u, 0x001Fu};
    TEST_ASSERT_TRUE(ppm_dump("ppm_dump_selftest.ppm", img, 2, 1));
    FILE *f = fopen("ppm_dump_selftest.ppm", "rb");
    TEST_ASSERT_NOT_NULL(f);
    unsigned char buf[32];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    (void)remove("ppm_dump_selftest.ppm");
    TEST_ASSERT_EQUAL_size_t(11 + 6, n);
    TEST_ASSERT_EQUAL_MEMORY("P6\n2 1\n255\n", buf, 11);
    const unsigned char pix[6] = {255, 0, 0, 0, 0, 255};
    TEST_ASSERT_EQUAL_MEMORY(pix, buf + 11, 6);
    TEST_ASSERT_FALSE(ppm_dump("x.ppm", img, 0, 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_surface_init_clips_to_its_own_logical_area);
    RUN_TEST(test_rgb565_macro);
    RUN_TEST(test_fill_paints_exactly_its_rectangle);
    RUN_TEST(test_fill_covering_the_whole_buffer_and_more);
    RUN_TEST(test_fill_is_clipped_at_each_edge);
    RUN_TEST(test_fully_outside_rectangles_write_nothing);
    RUN_TEST(test_zero_and_negative_sizes_draw_nothing);
    RUN_TEST(test_no_int16_overflow_near_the_limits);
    RUN_TEST(test_null_surface_pixels_and_bits_are_ignored);
    RUN_TEST(test_hline_and_vline_boundary_pixels);
    RUN_TEST(test_rect_draws_only_the_outline);
    RUN_TEST(test_degenerate_rects);
    RUN_TEST(test_rect_clipped_by_the_edge_keeps_its_visible_sides);
    RUN_TEST(test_narrowed_clip_limits_every_primitive);
    RUN_TEST(test_blit_opaque_paints_fg_and_bg);
    RUN_TEST(test_blit_transparent_leaves_clear_bits_alone);
    RUN_TEST(test_blit_clipped_at_left_and_top_uses_the_right_source_bits);
    RUN_TEST(test_blit_clipped_at_right_and_bottom);
    RUN_TEST(test_same_logical_draw_lands_in_the_right_rows_of_every_band);
    RUN_TEST(test_draw_entirely_outside_a_band_is_rejected);
    RUN_TEST(test_origin_x_translates_horizontally);
    RUN_TEST(test_ppm_dump_writes_a_p6_image);
    return UNITY_END();
}
