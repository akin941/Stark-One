/*
 * test_err.c — host unit test for stark_err_str()
 * Compiles with the host test harness (STARK-0005).
 */
#include "stark_err.h"
#include "unity.h"
#include <string.h>

void test_stark_err_str_all_codes_non_null(void)
{
    for (int i = STARK_OK; i <= STARK_ERR_STATE; i++) {
        const char *s = stark_err_str((stark_err_t)i);
        TEST_ASSERT_NOT_NULL_MESSAGE(s, "stark_err_str returned NULL");
        TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, (int)strlen(s),
                                             "stark_err_str returned empty string");
    }
}

void test_stark_err_str_all_codes_distinct(void)
{
    const char *strings[STARK_ERR_STATE + 1];
    for (int i = STARK_OK; i <= STARK_ERR_STATE; i++) {
        strings[i] = stark_err_str((stark_err_t)i);
    }

    for (int i = STARK_OK; i <= STARK_ERR_STATE; i++) {
        for (int j = i + 1; j <= STARK_ERR_STATE; j++) {
            /* strcmp returns 0 when equal, so test for != 0 */
            TEST_ASSERT_NOT_EQUAL(0, strcmp(strings[i], strings[j]));
        }
    }
}

void test_stark_err_str_known_values(void)
{
    TEST_ASSERT_EQUAL_STRING("OK", stark_err_str(STARK_OK));
    TEST_ASSERT_EQUAL_STRING("Invalid argument", stark_err_str(STARK_ERR_INVALID_ARG));
    TEST_ASSERT_EQUAL_STRING("Out of memory", stark_err_str(STARK_ERR_NO_MEM));
    TEST_ASSERT_EQUAL_STRING("Timeout", stark_err_str(STARK_ERR_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("Not found", stark_err_str(STARK_ERR_NOT_FOUND));
    TEST_ASSERT_EQUAL_STRING("Not supported", stark_err_str(STARK_ERR_NOT_SUPPORTED));
    TEST_ASSERT_EQUAL_STRING("Busy", stark_err_str(STARK_ERR_BUSY));
    TEST_ASSERT_EQUAL_STRING("I/O error", stark_err_str(STARK_ERR_IO));
    TEST_ASSERT_EQUAL_STRING("Invalid state", stark_err_str(STARK_ERR_STATE));
}

/* Unity requires setUp and tearDown functions */
void setUp(void)
{}

void tearDown(void)
{}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stark_err_str_all_codes_non_null);
    RUN_TEST(test_stark_err_str_all_codes_distinct);
    RUN_TEST(test_stark_err_str_known_values);
    return UNITY_END();
}