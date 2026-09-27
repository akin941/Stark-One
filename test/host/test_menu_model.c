/*
 * test_menu_model.c — host tests for the list-menu model (STARK-0018,
 * TESTING.md §2 "menu model").
 */
#include "ui_menu_model.h"
#include "unity.h"

static ui_menu_model_t s_m;
static bool s_disabled[32];

static bool enabled(size_t i, void *ctx)
{
    (void)ctx;
    return !s_disabled[i];
}

static void init(size_t count, size_t visible)
{
    ui_menu_model_init(&s_m, count, visible, enabled, NULL);
}

static void assert_at(size_t sel, size_t top)
{
    TEST_ASSERT_TRUE(ui_menu_model_has_selection(&s_m));
    TEST_ASSERT_EQUAL_size_t(sel, ui_menu_model_selected(&s_m));
    TEST_ASSERT_EQUAL_size_t(top, ui_menu_model_top(&s_m));
}

void setUp(void)
{
    for (size_t i = 0; i < 32; i++) {
        s_disabled[i] = false;
    }
}

void tearDown(void)
{}

void test_init_selects_the_first_item_at_the_top(void)
{
    init(12, 9);
    assert_at(0, 0);
}

void test_selection_wraps_both_ways(void)
{
    init(12, 9);
    TEST_ASSERT_TRUE(ui_menu_model_move(&s_m, -1)); /* UP from the first */
    assert_at(11, 3);                               /* window at the end */
    TEST_ASSERT_TRUE(ui_menu_model_move(&s_m, +1)); /* DOWN from the last */
    assert_at(0, 0);
}

void test_scroll_window_follows_the_selection(void)
{
    init(12, 9);
    for (size_t i = 1; i <= 8; i++) {
        TEST_ASSERT_TRUE(ui_menu_model_move(&s_m, +1));
        assert_at(i, 0); /* still inside the first window */
    }
    TEST_ASSERT_TRUE(ui_menu_model_move(&s_m, +1));
    assert_at(9, 1); /* scrolled by one */
    TEST_ASSERT_TRUE(ui_menu_model_move(&s_m, +1));
    assert_at(10, 2);
    for (size_t i = 0; i < 9; i++) {
        TEST_ASSERT_TRUE(ui_menu_model_move(&s_m, -1));
    }
    assert_at(1, 1); /* the window only moves once the selection leaves it */
    TEST_ASSERT_TRUE(ui_menu_model_move(&s_m, -1));
    assert_at(0, 0);
}

void test_paging_stops_at_the_list_boundaries(void)
{
    init(20, 9);
    TEST_ASSERT_TRUE(ui_menu_model_page(&s_m, +1));
    assert_at(9, 1);
    TEST_ASSERT_TRUE(ui_menu_model_page(&s_m, +1));
    assert_at(18, 10);
    TEST_ASSERT_TRUE(ui_menu_model_page(&s_m, +1));
    assert_at(19, 11); /* clamped to the last item, no wrap */
    TEST_ASSERT_FALSE(ui_menu_model_page(&s_m, +1));
    assert_at(19, 11);
    TEST_ASSERT_TRUE(ui_menu_model_page(&s_m, -1));
    assert_at(10, 10);
    TEST_ASSERT_TRUE(ui_menu_model_page(&s_m, -1));
    assert_at(1, 1);
    TEST_ASSERT_TRUE(ui_menu_model_page(&s_m, -1));
    assert_at(0, 0); /* clamped to the first */
    TEST_ASSERT_FALSE(ui_menu_model_page(&s_m, -1));
    TEST_ASSERT_FALSE(ui_menu_model_page(&s_m, 0));
    TEST_ASSERT_FALSE(ui_menu_model_move(&s_m, 0));
}

void test_empty_list_has_no_selection_and_ignores_keys(void)
{
    init(0, 9);
    TEST_ASSERT_FALSE(ui_menu_model_has_selection(&s_m));
    TEST_ASSERT_FALSE(ui_menu_model_move(&s_m, +1));
    TEST_ASSERT_FALSE(ui_menu_model_page(&s_m, +1));
    TEST_ASSERT_EQUAL_size_t(0, ui_menu_model_top(&s_m));
}

void test_list_shorter_than_the_window_never_scrolls(void)
{
    init(3, 9);
    assert_at(0, 0);
    TEST_ASSERT_TRUE(ui_menu_model_page(&s_m, +1));
    assert_at(2, 0);
    TEST_ASSERT_TRUE(ui_menu_model_move(&s_m, +1));
    assert_at(0, 0);
    TEST_ASSERT_TRUE(ui_menu_model_move(&s_m, -1));
    assert_at(2, 0);
    TEST_ASSERT_TRUE(ui_menu_model_page(&s_m, -1));
    assert_at(0, 0);
}

void test_single_item_list(void)
{
    init(1, 9);
    assert_at(0, 0);
    TEST_ASSERT_FALSE(ui_menu_model_move(&s_m, +1));
    TEST_ASSERT_FALSE(ui_menu_model_move(&s_m, -1));
    TEST_ASSERT_FALSE(ui_menu_model_page(&s_m, +1));
    TEST_ASSERT_FALSE(ui_menu_model_page(&s_m, -1));
    assert_at(0, 0);
}

void test_disabled_items_are_skipped(void)
{
    s_disabled[0] = true; /* the first item: init skips it too */
    s_disabled[2] = true;
    s_disabled[3] = true;
    s_disabled[11] = true;
    init(12, 9);
    assert_at(1, 0);
    TEST_ASSERT_TRUE(ui_menu_model_move(&s_m, +1));
    assert_at(4, 0); /* over 2 and 3 */
    TEST_ASSERT_TRUE(ui_menu_model_move(&s_m, -1));
    assert_at(1, 0);
    TEST_ASSERT_TRUE(ui_menu_model_move(&s_m, -1)); /* wraps past 0 and 11 */
    assert_at(10, 2);
    TEST_ASSERT_TRUE(ui_menu_model_move(&s_m, +1)); /* wraps past 11 and 0 */
    assert_at(1, 1); /* the window scrolls just enough to show item 1 */
}

void test_paging_onto_disabled_items_lands_on_the_nearest_enabled(void)
{
    s_disabled[9] = true;
    s_disabled[10] = true;
    init(12, 9);
    TEST_ASSERT_TRUE(ui_menu_model_page(&s_m, +1)); /* 0 + 9 = 9 disabled -> 11 */
    assert_at(11, 3);
    setUp();
    for (size_t i = 5; i < 12; i++) {
        s_disabled[i] = true; /* nothing enabled past 4 */
    }
    init(12, 9);
    TEST_ASSERT_TRUE(ui_menu_model_page(&s_m, +1)); /* falls back towards the start */
    assert_at(4, 0);
    TEST_ASSERT_FALSE(ui_menu_model_page(&s_m, +1));
}

void test_all_items_disabled_means_no_selection(void)
{
    for (size_t i = 0; i < 5; i++) {
        s_disabled[i] = true;
    }
    init(5, 9);
    TEST_ASSERT_FALSE(ui_menu_model_has_selection(&s_m));
    TEST_ASSERT_FALSE(ui_menu_model_move(&s_m, +1));
    TEST_ASSERT_FALSE(ui_menu_model_page(&s_m, -1));
}

void test_null_enabled_callback_means_all_enabled_and_zero_visible_means_one(void)
{
    ui_menu_model_init(&s_m, 4, 0, NULL, NULL);
    assert_at(0, 0);
    TEST_ASSERT_TRUE(ui_menu_model_move(&s_m, +1));
    assert_at(1, 1); /* a one-row window scrolls with every move */
}

void test_null_model_is_safe(void)
{
    ui_menu_model_init(NULL, 3, 3, NULL, NULL);
    TEST_ASSERT_FALSE(ui_menu_model_move(NULL, 1));
    TEST_ASSERT_FALSE(ui_menu_model_page(NULL, 1));
    TEST_ASSERT_FALSE(ui_menu_model_has_selection(NULL));
    TEST_ASSERT_EQUAL_size_t(0, ui_menu_model_selected(NULL));
    TEST_ASSERT_EQUAL_size_t(0, ui_menu_model_top(NULL));
}

void test_window_invariant_holds_under_random_operations(void)
{
    uint32_t rng = 12345;
    for (size_t count = 1; count <= 25; count++) {
        for (size_t i = 0; i < 32; i++) {
            rng = rng * 1664525u + 1013904223u;
            s_disabled[i] = ((rng >> 16) % 4u) == 0; /* ~25 % disabled */
        }
        init(count, 9);
        for (int step = 0; step < 400; step++) {
            rng = rng * 1664525u + 1013904223u;
            unsigned op = (rng >> 16) % 4u;
            if (op < 2) {
                (void)ui_menu_model_move(&s_m, op == 0 ? -1 : +1);
            } else {
                (void)ui_menu_model_page(&s_m, op == 2 ? -1 : +1);
            }
            if (!ui_menu_model_has_selection(&s_m)) {
                continue;
            }
            size_t sel = ui_menu_model_selected(&s_m), top = ui_menu_model_top(&s_m);
            TEST_ASSERT_FALSE(s_disabled[sel]);
            TEST_ASSERT_TRUE(top <= sel && sel < top + 9);
            if (count > 9) {
                TEST_ASSERT_TRUE(top + 9 <= count);
            } else {
                TEST_ASSERT_EQUAL_size_t(0, top);
            }
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_selects_the_first_item_at_the_top);
    RUN_TEST(test_selection_wraps_both_ways);
    RUN_TEST(test_scroll_window_follows_the_selection);
    RUN_TEST(test_paging_stops_at_the_list_boundaries);
    RUN_TEST(test_empty_list_has_no_selection_and_ignores_keys);
    RUN_TEST(test_list_shorter_than_the_window_never_scrolls);
    RUN_TEST(test_single_item_list);
    RUN_TEST(test_disabled_items_are_skipped);
    RUN_TEST(test_paging_onto_disabled_items_lands_on_the_nearest_enabled);
    RUN_TEST(test_all_items_disabled_means_no_selection);
    RUN_TEST(test_null_enabled_callback_means_all_enabled_and_zero_visible_means_one);
    RUN_TEST(test_null_model_is_safe);
    RUN_TEST(test_window_invariant_holds_under_random_operations);
    return UNITY_END();
}
