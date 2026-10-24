/*
 * test_ir_names.c — capture file names (TASKS.md STARK-0305).
 */
#include <string.h>
#include "ir_names.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static void test_next_name(void)
{
    ir_names_t st;
    char out[16];
    ir_names_init(&st);
    TEST_ASSERT_EQUAL(STARK_OK, ir_names_next(&st, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("ir_0001.ir", out);
    TEST_ASSERT_TRUE(ir_names_scan(&st, "ir_0007.ir"));
    TEST_ASSERT_TRUE(ir_names_scan(&st, "ir_0003.ir")); /* gaps are not refilled */
    TEST_ASSERT_EQUAL(STARK_OK, ir_names_next(&st, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("ir_0008.ir", out);
}

static void test_other_names_ignored(void)
{
    ir_names_t st;
    ir_names_init(&st);
    static const char *const others[] = {"wtest.bin",  "ir_12.ir",    "ir_00a1.ir", "ir_0001.txt",
                                         "IR_0002.ir", "ir_00011.ir", "xr_0003.ir"};
    for (size_t i = 0; i < sizeof others / sizeof others[0]; i++) {
        TEST_ASSERT_FALSE_MESSAGE(ir_names_scan(&st, others[i]), others[i]);
    }
    TEST_ASSERT_FALSE(ir_names_scan(&st, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, st.highest);
}

static void test_limits(void)
{
    ir_names_t st;
    char out[16];
    ir_names_init(&st);
    TEST_ASSERT_TRUE(ir_names_scan(&st, "ir_9998.ir"));
    TEST_ASSERT_EQUAL(STARK_OK, ir_names_next(&st, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("ir_9999.ir", out);
    TEST_ASSERT_TRUE(ir_names_scan(&st, "ir_9999.ir"));
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, ir_names_next(&st, out, sizeof out));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_names_next(&st, out, 10));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, ir_names_next(&st, NULL, 16));
}

static void test_browse_order(void)
{
    /* saved captures first, newest first */
    TEST_ASSERT_TRUE(ir_names_compare("ir_0002.ir", "ir_0001.ir") < 0);
    TEST_ASSERT_TRUE(ir_names_compare("ir_0001.ir", "ir_0002.ir") > 0);
    /* then everything else, by name */
    TEST_ASSERT_TRUE(ir_names_compare("ir_0001.ir", "lab_bad.ir") < 0);
    TEST_ASSERT_TRUE(ir_names_compare("lab_bad.ir", "ir_9999.ir") > 0);
    TEST_ASSERT_TRUE(ir_names_compare("lab_bad.ir", "lab_nec.ir") < 0);
    TEST_ASSERT_TRUE(ir_names_compare("ir_12.ir", "ir_0001.ir") > 0); /* not a capture name */
    TEST_ASSERT_EQUAL(0, ir_names_compare("lab_nec.ir", "lab_nec.ir"));
    TEST_ASSERT_EQUAL(0, ir_names_compare("ir_0007.ir", "ir_0007.ir"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_next_name);
    RUN_TEST(test_other_names_ignored);
    RUN_TEST(test_limits);
    RUN_TEST(test_browse_order);
    return UNITY_END();
}
