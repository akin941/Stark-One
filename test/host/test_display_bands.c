/*
 * test_display_bands.c — host tests for stark_display's band walk
 * (STARK-0016): which bands an area intersects and the origin/extent each
 * band renders.
 */
#include "display_bands.h"
#include "unity.h"

#define SW   320
#define SH   240
#define BH   40
#define MAXB 16

static gfx_rect_t s_out[MAXB];

static gfx_rect_t R(int x, int y, int w, int h)
{
    return (gfx_rect_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
}

static void assert_band(size_t i, int x, int y, int w, int h)
{
    TEST_ASSERT_EQUAL_INT16(x, s_out[i].x);
    TEST_ASSERT_EQUAL_INT16(y, s_out[i].y);
    TEST_ASSERT_EQUAL_INT16(w, s_out[i].w);
    TEST_ASSERT_EQUAL_INT16(h, s_out[i].h);
}

static size_t walk(gfx_rect_t area)
{
    return display_bands(area, SW, SH, BH, s_out, MAXB);
}

void setUp(void)
{}

void tearDown(void)
{}

void test_area_inside_one_band(void)
{
    TEST_ASSERT_EQUAL_size_t(1, walk(R(100, 45, 50, 30)));
    assert_band(0, 100, 45, 50, 30); /* band 1, origin = the area's corner */
}

void test_area_filling_exactly_one_band(void)
{
    TEST_ASSERT_EQUAL_size_t(1, walk(R(0, 40, SW, 40)));
    assert_band(0, 0, 40, SW, 40);
    TEST_ASSERT_EQUAL_size_t(1, walk(R(0, 0, SW, 40))); /* ends exactly on a boundary */
    assert_band(0, 0, 0, SW, 40);
}

void test_area_spanning_exactly_a_boundary(void)
{
    TEST_ASSERT_EQUAL_size_t(2, walk(R(10, 30, 50, 20))); /* rows 30..49 */
    assert_band(0, 10, 30, 50, 10);                       /* band 0: rows 30..39 */
    assert_band(1, 10, 40, 50, 10);                       /* band 1: rows 40..49 */
    TEST_ASSERT_EQUAL_size_t(2, walk(R(0, 39, 1, 2)));    /* one row each side */
    assert_band(0, 0, 39, 1, 1);
    assert_band(1, 0, 40, 1, 1);
}

void test_full_screen_walks_every_band_with_its_origin(void)
{
    TEST_ASSERT_EQUAL_size_t(6, walk(R(0, 0, SW, SH)));
    for (size_t i = 0; i < 6; i++) {
        assert_band(i, 0, (int)i * BH, SW, BH);
    }
}

void test_empty_areas_walk_nothing(void)
{
    TEST_ASSERT_EQUAL_size_t(0, walk(R(10, 10, 0, 10)));
    TEST_ASSERT_EQUAL_size_t(0, walk(R(10, 10, 10, 0)));
    TEST_ASSERT_EQUAL_size_t(0, walk(R(10, 10, -5, 10)));
    TEST_ASSERT_EQUAL_size_t(0, walk(R(10, 10, 10, -5)));
}

void test_out_of_bounds_areas_walk_nothing(void)
{
    TEST_ASSERT_EQUAL_size_t(0, walk(R(SW, 0, 10, 10)));      /* starts at the right edge */
    TEST_ASSERT_EQUAL_size_t(0, walk(R(0, SH, 10, 10)));      /* starts at the bottom edge */
    TEST_ASSERT_EQUAL_size_t(0, walk(R(-20, 0, 20, 10)));     /* ends at the left edge */
    TEST_ASSERT_EQUAL_size_t(0, walk(R(0, -20, 10, 20)));     /* ends at the top edge */
    TEST_ASSERT_EQUAL_size_t(0, walk(R(32000, 0, 32000, 5))); /* x + w overflows int16 */
    TEST_ASSERT_EQUAL_size_t(0, walk(R(-32768, 0, 32767, 5)));
}

void test_partially_off_screen_areas_are_clipped(void)
{
    TEST_ASSERT_EQUAL_size_t(1, walk(R(-10, 5, 30, 10)));
    assert_band(0, 0, 5, 20, 10);
    TEST_ASSERT_EQUAL_size_t(1, walk(R(300, 230, 50, 50)));
    assert_band(0, 300, 230, 20, 10);
    TEST_ASSERT_EQUAL_size_t(6, walk(R(-100, -100, 1000, 1000)));
    assert_band(5, 0, 200, SW, BH);
}

void test_band_height_that_does_not_divide_the_screen(void)
{
    TEST_ASSERT_EQUAL_size_t(4, display_bands(R(0, 0, SW, SH), SW, SH, 64, s_out, MAXB));
    assert_band(3, 0, 192, SW, 48); /* last band is the 48-row remainder */
}

void test_output_is_truncated_to_max_out_and_bad_arguments_yield_zero(void)
{
    TEST_ASSERT_EQUAL_size_t(2, display_bands(R(0, 0, SW, SH), SW, SH, BH, s_out, 2));
    assert_band(1, 0, 40, SW, BH);
    TEST_ASSERT_EQUAL_size_t(0, display_bands(R(0, 0, SW, SH), SW, SH, BH, NULL, MAXB));
    TEST_ASSERT_EQUAL_size_t(0, display_bands(R(0, 0, SW, SH), SW, SH, BH, s_out, 0));
    TEST_ASSERT_EQUAL_size_t(0, display_bands(R(0, 0, SW, SH), SW, SH, 0, s_out, MAXB));
    TEST_ASSERT_EQUAL_size_t(0, display_bands(R(0, 0, SW, SH), 0, SH, BH, s_out, MAXB));
    TEST_ASSERT_EQUAL_size_t(0, display_bands(R(0, 0, SW, SH), SW, -1, BH, s_out, MAXB));
}

void test_bands_tile_the_area_exactly(void)
{
    /* For many areas, the band rectangles are disjoint, in order, and their
     * union is exactly the clipped area. */
    for (int y = -50; y < SH + 10; y += 7) {
        for (int h = 1; h < 130; h += 11) {
            size_t n = walk(R(3, y, 17, h));
            int top = y < 0 ? 0 : y;
            int bottom = y + h > SH ? SH : y + h;
            if (top >= bottom) {
                TEST_ASSERT_EQUAL_size_t(0, n);
                continue;
            }
            TEST_ASSERT_GREATER_THAN_size_t(0, n);
            int row = top;
            for (size_t i = 0; i < n; i++) {
                TEST_ASSERT_EQUAL_INT16(row, s_out[i].y);
                TEST_ASSERT_EQUAL_INT(s_out[i].y / BH, (s_out[i].y + s_out[i].h - 1) / BH);
                row += s_out[i].h;
            }
            TEST_ASSERT_EQUAL_INT(bottom, row);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_area_inside_one_band);
    RUN_TEST(test_area_filling_exactly_one_band);
    RUN_TEST(test_area_spanning_exactly_a_boundary);
    RUN_TEST(test_full_screen_walks_every_band_with_its_origin);
    RUN_TEST(test_empty_areas_walk_nothing);
    RUN_TEST(test_out_of_bounds_areas_walk_nothing);
    RUN_TEST(test_partially_off_screen_areas_are_clipped);
    RUN_TEST(test_band_height_that_does_not_divide_the_screen);
    RUN_TEST(test_output_is_truncated_to_max_out_and_bad_arguments_yield_zero);
    RUN_TEST(test_bands_tile_the_area_exactly);
    return UNITY_END();
}
