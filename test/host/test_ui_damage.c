/*
 * test_ui_damage.c — the UI damage set (TASKS.md STARK-0101).
 */
#include <stdio.h>
#include <string.h>
#include "ui_damage.h"
#include "unity.h"

static ui_damage_t d;

void setUp(void)
{
    memset(&d, 0, sizeof d);
}

void tearDown(void)
{}

static gfx_rect_t R(int x, int y, int w, int h)
{
    return (gfx_rect_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
}

static void assert_rect(gfx_rect_t want, gfx_rect_t got)
{
    char msg[64];
    snprintf(msg, sizeof msg, "got %d,%d %dx%d", got.x, got.y, got.w, got.h);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(want.x, got.x, msg);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(want.y, got.y, msg);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(want.w, got.w, msg);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(want.h, got.h, msg);
}

static void test_zero_initialised_is_empty(void)
{
    TEST_ASSERT_TRUE(ui_damage_empty(&d));
    ui_damage_add(&d, R(0, 0, 1, 1));
    TEST_ASSERT_FALSE(ui_damage_empty(&d));
    ui_damage_clear(&d);
    TEST_ASSERT_TRUE(ui_damage_empty(&d));
}

static void test_empty_and_negative_ignored(void)
{
    ui_damage_add(&d, R(5, 5, 0, 10));
    ui_damage_add(&d, R(5, 5, 10, 0));
    ui_damage_add(&d, R(5, 5, -3, 10));
    ui_damage_add(&d, R(5, 5, 10, -1));
    TEST_ASSERT_EQUAL_UINT8(0, d.n);
}

static void test_disjoint_stay_separate(void)
{
    ui_damage_add(&d, R(0, 16, 320, 24));
    ui_damage_add(&d, R(0, 88, 320, 24)); /* the menu wrap: rows 0 and 3 */
    TEST_ASSERT_EQUAL_UINT8(2, d.n);
    assert_rect(R(0, 16, 320, 24), d.r[0]);
    assert_rect(R(0, 88, 320, 24), d.r[1]);
}

static void test_shared_edge_merges(void)
{
    ui_damage_add(&d, R(0, 16, 320, 24));
    ui_damage_add(&d, R(0, 40, 320, 24)); /* adjacent rows */
    TEST_ASSERT_EQUAL_UINT8(1, d.n);
    assert_rect(R(0, 16, 320, 48), d.r[0]);
    ui_damage_add(&d, R(320, 16, 10, 5)); /* touching the right edge */
    TEST_ASSERT_EQUAL_UINT8(1, d.n);
    assert_rect(R(0, 16, 330, 48), d.r[0]);
}

static void test_corner_contact_does_not_merge(void)
{
    ui_damage_add(&d, R(0, 0, 10, 10));
    ui_damage_add(&d, R(10, 10, 10, 10));
    TEST_ASSERT_EQUAL_UINT8(2, d.n);
}

static void test_overlap_merges(void)
{
    ui_damage_add(&d, R(10, 10, 20, 20));
    ui_damage_add(&d, R(25, 25, 20, 20));
    TEST_ASSERT_EQUAL_UINT8(1, d.n);
    assert_rect(R(10, 10, 35, 35), d.r[0]);
}

static void test_contained_rect_changes_nothing(void)
{
    ui_damage_add(&d, R(0, 0, 100, 100));
    ui_damage_add(&d, R(10, 10, 5, 5));
    TEST_ASSERT_EQUAL_UINT8(1, d.n);
    assert_rect(R(0, 0, 100, 100), d.r[0]);
}

static void test_bridge_cascades(void)
{
    ui_damage_add(&d, R(0, 0, 10, 10));
    ui_damage_add(&d, R(30, 0, 10, 10));
    ui_damage_add(&d, R(100, 100, 5, 5));
    TEST_ASSERT_EQUAL_UINT8(3, d.n);
    ui_damage_add(&d, R(5, 2, 30, 4)); /* overlaps the first two */
    TEST_ASSERT_EQUAL_UINT8(2, d.n);
    assert_rect(R(100, 100, 5, 5), d.r[0]);
    assert_rect(R(0, 0, 40, 10), d.r[1]);
}

static void test_growth_after_merge_cascades(void)
{
    /* A union's bounding box can reach a third rect it did not touch before. */
    ui_damage_add(&d, R(0, 0, 10, 10));
    ui_damage_add(&d, R(20, 20, 10, 10));
    ui_damage_add(&d, R(0, 20, 5, 5));
    TEST_ASSERT_EQUAL_UINT8(3, d.n);
    ui_damage_add(&d, R(5, 5, 20, 20)); /* joins 0 and 1; the box then covers 2 */
    TEST_ASSERT_EQUAL_UINT8(1, d.n);
    assert_rect(R(0, 0, 30, 30), d.r[0]);
}

static void test_fifth_rect_merges_cheapest_pair(void)
{
    ui_damage_add(&d, R(0, 0, 10, 10));
    ui_damage_add(&d, R(100, 0, 10, 10));
    ui_damage_add(&d, R(200, 0, 10, 10));
    ui_damage_add(&d, R(0, 100, 10, 10));
    ui_damage_add(&d, R(0, 112, 10, 10)); /* 2 px under the 4th: cheapest merge */
    TEST_ASSERT_EQUAL_UINT8(4, d.n);
    assert_rect(R(0, 0, 10, 10), d.r[0]);
    assert_rect(R(100, 0, 10, 10), d.r[1]);
    assert_rect(R(200, 0, 10, 10), d.r[2]);
    assert_rect(R(0, 100, 10, 22), d.r[3]);
}

static void test_fifth_rect_tie_takes_lowest_pair(void)
{
    /* Five equal squares, equally spaced in a row: every neighbour pair
     * costs the same; the lowest index pair (0, 1) merges. */
    for (int i = 0; i < 5; i++) {
        ui_damage_add(&d, R(i * 20, 0, 10, 10));
    }
    TEST_ASSERT_EQUAL_UINT8(4, d.n);
    assert_rect(R(40, 0, 10, 10), d.r[0]);
    assert_rect(R(60, 0, 10, 10), d.r[1]);
    assert_rect(R(80, 0, 10, 10), d.r[2]);
    assert_rect(R(0, 0, 30, 10), d.r[3]);
}

static void test_full_merge_can_cascade(void)
{
    /* The cheapest merge's box swallows another stored rect. */
    ui_damage_add(&d, R(0, 0, 4, 4));
    ui_damage_add(&d, R(10, 10, 4, 4));
    ui_damage_add(&d, R(5, 5, 2, 2));
    ui_damage_add(&d, R(200, 200, 4, 4));
    ui_damage_add(&d, R(100, 0, 4, 4));
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(UI_DAMAGE_MAX, d.n);
    for (uint8_t i = 0; i < d.n; i++) {
        for (uint8_t j = (uint8_t)(i + 1); j < d.n; j++) {
            gfx_rect_t a = d.r[i];
            gfx_rect_t b = d.r[j];
            TEST_ASSERT_FALSE(a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h &&
                              b.y < a.y + a.h);
        }
    }
}

/* ---- property test ----------------------------------------------------- */

#define CW 64
#define CH 48

static uint32_t s_seed;

static int rnd(int n)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return (int)((s_seed >> 16) % (uint32_t)n);
}

static void test_property_no_overlap_full_coverage(void)
{
    s_seed = 0x57A2C0DEu;
    for (int seq = 0; seq < 1000; seq++) {
        bool added[CH][CW];
        memset(added, 0, sizeof added);
        ui_damage_clear(&d);
        int adds = 1 + rnd(9);
        for (int a = 0; a < adds; a++) {
            gfx_rect_t r = R(rnd(CW) - 4, rnd(CH) - 4, rnd(24) - 2, rnd(18) - 2);
            ui_damage_add(&d, r);
            for (int y = r.y; r.w > 0 && y < r.y + r.h; y++) {
                for (int x = r.x; x < r.x + r.w; x++) {
                    if (x >= 0 && y >= 0 && x < CW && y < CH) {
                        added[y][x] = true;
                    }
                }
            }
            TEST_ASSERT_LESS_OR_EQUAL_UINT8(UI_DAMAGE_MAX, d.n);
            for (uint8_t i = 0; i < d.n; i++) {
                TEST_ASSERT_TRUE(d.r[i].w > 0 && d.r[i].h > 0);
                for (uint8_t j = (uint8_t)(i + 1); j < d.n; j++) {
                    gfx_rect_t p = d.r[i];
                    gfx_rect_t q = d.r[j];
                    TEST_ASSERT_FALSE_MESSAGE(p.x < q.x + q.w && q.x < p.x + p.w &&
                                                  p.y < q.y + q.h && q.y < p.y + p.h,
                                              "stored rects overlap");
                }
            }
            for (int y = 0; y < CH; y++) {
                for (int x = 0; x < CW; x++) {
                    if (!added[y][x]) {
                        continue;
                    }
                    bool covered = false;
                    for (uint8_t i = 0; i < d.n && !covered; i++) {
                        covered = x >= d.r[i].x && x < d.r[i].x + d.r[i].w && y >= d.r[i].y &&
                                  y < d.r[i].y + d.r[i].h;
                    }
                    TEST_ASSERT_TRUE_MESSAGE(covered, "added pixel not covered");
                }
            }
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_zero_initialised_is_empty);
    RUN_TEST(test_empty_and_negative_ignored);
    RUN_TEST(test_disjoint_stay_separate);
    RUN_TEST(test_shared_edge_merges);
    RUN_TEST(test_corner_contact_does_not_merge);
    RUN_TEST(test_overlap_merges);
    RUN_TEST(test_contained_rect_changes_nothing);
    RUN_TEST(test_bridge_cascades);
    RUN_TEST(test_growth_after_merge_cascades);
    RUN_TEST(test_fifth_rect_merges_cheapest_pair);
    RUN_TEST(test_fifth_rect_tie_takes_lowest_pair);
    RUN_TEST(test_full_merge_can_cascade);
    RUN_TEST(test_property_no_overlap_full_coverage);
    return UNITY_END();
}
