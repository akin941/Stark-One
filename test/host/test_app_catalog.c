/*
 * test_app_catalog.c — host tests for the app registry helpers
 * (STARK-0019): launcher order (categories in registry order since
 * STARK-0107, titles within, stable), the header/app row layout, lookup.
 */
#include "app_internal.h"
#include "unity.h"

#define APP(i, t, c)                                                                               \
    {                                                                                              \
        .id = (i), .title = (t), .category = (c)                                                   \
    }

static const stark_app_t a_about = APP("about", "About", "System");
static const stark_app_t a_input = APP("inputtest", "Input Test", "Lab");
static const stark_app_t a_display = APP("displaytest", "Display Test", "Lab");
static const stark_app_t a_buzzer = APP("buzzertest", "Buzzer Test", "Lab");
static const stark_app_t a_twin1 = APP("twin1", "Same", "Lab"); /* equal keys */
static const stark_app_t a_twin2 = APP("twin2", "Same", "Lab");
static const stark_app_t a_nocat = APP("nocat", "Zeta", NULL); /* NULL sorts as "" */

void setUp(void)
{}

void tearDown(void)
{}

void test_categories_follow_the_registry_titles_sort_within(void)
{
    const stark_app_t *const reg[] = {&a_about, &a_input, &a_display, &a_buzzer};
    const stark_app_t *out[4];
    TEST_ASSERT_EQUAL_size_t(4, app_catalog_sort(reg, 4, out, 4));
    TEST_ASSERT_EQUAL_PTR(&a_about, out[0]);   /* System: its first app is registry #0 */
    TEST_ASSERT_EQUAL_PTR(&a_buzzer, out[1]);  /* Lab: Buzzer Test */
    TEST_ASSERT_EQUAL_PTR(&a_display, out[2]); /* Lab: Display Test */
    TEST_ASSERT_EQUAL_PTR(&a_input, out[3]);   /* Lab: Input Test */

    const stark_app_t *const lab_first[] = {&a_input, &a_about, &a_buzzer};
    app_catalog_sort(lab_first, 3, out, 3);
    TEST_ASSERT_EQUAL_PTR(&a_buzzer, out[0]); /* Lab first now: its first app leads */
    TEST_ASSERT_EQUAL_PTR(&a_input, out[1]);
    TEST_ASSERT_EQUAL_PTR(&a_about, out[2]);
}

void test_equal_keys_keep_registry_order(void)
{
    const stark_app_t *const fwd[] = {&a_twin1, &a_about, &a_twin2};
    const stark_app_t *const rev[] = {&a_twin2, &a_about, &a_twin1};
    const stark_app_t *out[3];
    app_catalog_sort(fwd, 3, out, 3);
    TEST_ASSERT_EQUAL_PTR(&a_twin1, out[0]);
    TEST_ASSERT_EQUAL_PTR(&a_twin2, out[1]);
    app_catalog_sort(rev, 3, out, 3);
    TEST_ASSERT_EQUAL_PTR(&a_twin2, out[0]);
    TEST_ASSERT_EQUAL_PTR(&a_twin1, out[1]);
    TEST_ASSERT_EQUAL_PTR(&a_about, out[2]);
}

void test_null_category_groups_as_empty_in_registry_order(void)
{
    const stark_app_t *const reg[] = {&a_about, &a_nocat};
    const stark_app_t *out[2];
    app_catalog_sort(reg, 2, out, 2);
    TEST_ASSERT_EQUAL_PTR(&a_about, out[0]);
    TEST_ASSERT_EQUAL_PTR(&a_nocat, out[1]);
    const stark_app_t *const rev[] = {&a_nocat, &a_about};
    app_catalog_sort(rev, 2, out, 2);
    TEST_ASSERT_EQUAL_PTR(&a_nocat, out[0]);
}

void test_empty_and_single_registries(void)
{
    const stark_app_t *out[1] = {NULL};
    TEST_ASSERT_EQUAL_size_t(0, app_catalog_sort(NULL, 0, out, 1));
    const stark_app_t *const one[] = {&a_about};
    TEST_ASSERT_EQUAL_size_t(0, app_catalog_sort(one, 0, out, 1));
    TEST_ASSERT_EQUAL_size_t(1, app_catalog_sort(one, 1, out, 1));
    TEST_ASSERT_EQUAL_PTR(&a_about, out[0]);
    TEST_ASSERT_EQUAL_size_t(0, app_catalog_sort(one, 1, NULL, 1));
    TEST_ASSERT_NULL(app_catalog_find(one, 0, "about")); /* empty registry */
}

void test_output_is_limited_to_max(void)
{
    const stark_app_t *const reg[] = {&a_about, &a_input, &a_display};
    const stark_app_t *out[2];
    TEST_ASSERT_EQUAL_size_t(2, app_catalog_sort(reg, 3, out, 2));
    TEST_ASSERT_EQUAL_PTR(&a_about, out[0]); /* the first two, sorted */
    TEST_ASSERT_EQUAL_PTR(&a_input, out[1]);
}

void test_layout_puts_a_header_before_each_category(void)
{
    const stark_app_t *const reg[] = {&a_about, &a_input, &a_display, &a_nocat};
    const stark_app_t *sorted[4];
    app_catalog_sort(reg, 4, sorted, 4);
    app_catalog_row_t rows[8];
    TEST_ASSERT_EQUAL_size_t(7, app_catalog_layout(sorted, 4, rows, 8));
    TEST_ASSERT_EQUAL_STRING("System", rows[0].header);
    TEST_ASSERT_NULL(rows[0].app);
    TEST_ASSERT_EQUAL_PTR(&a_about, rows[1].app);
    TEST_ASSERT_NULL(rows[1].header);
    TEST_ASSERT_EQUAL_STRING("Lab", rows[2].header);
    TEST_ASSERT_EQUAL_PTR(&a_display, rows[3].app);
    TEST_ASSERT_EQUAL_PTR(&a_input, rows[4].app);
    TEST_ASSERT_EQUAL_STRING("", rows[5].header); /* the NULL category */
    TEST_ASSERT_EQUAL_PTR(&a_nocat, rows[6].app);
}

void test_layout_limits_and_empty(void)
{
    const stark_app_t *const sorted[] = {&a_about, &a_input};
    app_catalog_row_t rows[4];
    TEST_ASSERT_EQUAL_size_t(1, app_catalog_layout(sorted, 2, rows, 1)); /* header only */
    TEST_ASSERT_EQUAL_size_t(3, app_catalog_layout(sorted, 2, rows, 3)); /* hdr, app, hdr */
    TEST_ASSERT_NULL(rows[2].app);
    TEST_ASSERT_EQUAL_size_t(0, app_catalog_layout(sorted, 0, rows, 4));
    TEST_ASSERT_EQUAL_size_t(0, app_catalog_layout(NULL, 2, rows, 4));
    TEST_ASSERT_EQUAL_size_t(0, app_catalog_layout(sorted, 2, NULL, 4));
}

void test_find_by_id_including_not_found(void)
{
    const stark_app_t *const reg[] = {&a_about, &a_input, &a_display};
    TEST_ASSERT_EQUAL_PTR(&a_input, app_catalog_find(reg, 3, "inputtest"));
    TEST_ASSERT_EQUAL_PTR(&a_about, app_catalog_find(reg, 3, "about"));
    TEST_ASSERT_NULL(app_catalog_find(reg, 3, "nope"));
    TEST_ASSERT_NULL(app_catalog_find(reg, 3, "About")); /* ids are case-sensitive */
    TEST_ASSERT_NULL(app_catalog_find(reg, 3, "input")); /* no prefix matching */
    TEST_ASSERT_NULL(app_catalog_find(reg, 3, NULL));
    TEST_ASSERT_NULL(app_catalog_find(NULL, 3, "about"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_categories_follow_the_registry_titles_sort_within);
    RUN_TEST(test_equal_keys_keep_registry_order);
    RUN_TEST(test_null_category_groups_as_empty_in_registry_order);
    RUN_TEST(test_empty_and_single_registries);
    RUN_TEST(test_output_is_limited_to_max);
    RUN_TEST(test_layout_puts_a_header_before_each_category);
    RUN_TEST(test_layout_limits_and_empty);
    RUN_TEST(test_find_by_id_including_not_found);
    return UNITY_END();
}
