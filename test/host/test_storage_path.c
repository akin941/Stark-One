/*
 * test_storage_path.c — name rules, path building and errno mapping
 * (TASKS.md STARK-0203): every rejection class, exact output, truncation.
 */
#include <errno.h>
#include <string.h>
#include "storage_path.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static void test_valid_names(void)
{
    TEST_ASSERT_TRUE(storage_name_valid("a"));
    TEST_ASSERT_TRUE(storage_name_valid("ir_0001.sub"));
    TEST_ASSERT_TRUE(storage_name_valid("Log-2.TXT"));
    TEST_ASSERT_TRUE(storage_name_valid("abcdefghijklmnopqrstuvwxyz012345")); /* 32 */
    TEST_ASSERT_TRUE(storage_name_valid("x.tm"));
    TEST_ASSERT_TRUE(storage_name_valid("tmp"));
}

static void test_rejected_names(void)
{
    TEST_ASSERT_FALSE(storage_name_valid(NULL));
    TEST_ASSERT_FALSE(storage_name_valid(""));
    TEST_ASSERT_FALSE(storage_name_valid(".hidden"));
    TEST_ASSERT_FALSE(storage_name_valid(".."));
    TEST_ASSERT_FALSE(storage_name_valid("a/b"));
    TEST_ASSERT_FALSE(storage_name_valid("../etc"));
    TEST_ASSERT_FALSE(storage_name_valid("a\\b"));
    TEST_ASSERT_FALSE(storage_name_valid("sp ace"));
    TEST_ASSERT_FALSE(storage_name_valid("ü.txt"));
    TEST_ASSERT_FALSE(storage_name_valid("abcdefghijklmnopqrstuvwxyz0123456")); /* 33 */
    TEST_ASSERT_FALSE(storage_name_valid("half.tmp"));
    TEST_ASSERT_FALSE(storage_name_valid(".tmp"));
}

static void test_paths(void)
{
    char out[STORAGE_PATH_MAX];
    TEST_ASSERT_EQUAL(
        STARK_OK, storage_path_build("/sd", STARK_DIR_CAPTURES, "a.bin", false, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("/sd/stark/captures/a.bin", out);
    TEST_ASSERT_EQUAL(STARK_OK,
                      storage_path_build("/sd", STARK_DIR_CONFIG, "a.bin", true, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("/sd/stark/config/a.bin.tmp", out);
    TEST_ASSERT_EQUAL(STARK_OK, storage_dir_path("/sd", STARK_DIR_LOGS, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("/sd/stark/logs", out);
    TEST_ASSERT_EQUAL(STARK_OK, storage_dir_path("/sd", STARK_DIR_APPS, out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("/sd/stark/apps", out);
}

static void test_path_errors(void)
{
    char out[STORAGE_PATH_MAX];
    char tiny[16];
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      storage_path_build("/sd", STARK_DIR_LOGS, "../x", false, out, sizeof out));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      storage_path_build("/sd", (stark_dir_t)9, "x", false, out, sizeof out));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      storage_path_build(NULL, STARK_DIR_LOGS, "x", false, out, sizeof out));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      storage_dir_path("/sd", STARK_DIR_LOGS, NULL, sizeof out));
    /* directory part does not fit */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      storage_path_build("/sd", STARK_DIR_CAPTURES, "x", false, tiny, sizeof tiny));
    /* directory fits, name does not */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, storage_path_build("/sd", STARK_DIR_LOGS, "abcdef",
                                                                false, tiny, sizeof tiny));
    TEST_ASSERT_NULL(storage_dir_name((stark_dir_t)4));
}

static void test_errno_mapping(void)
{
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, storage_map_errno(ENOSPC));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, storage_map_errno(ENOENT));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, storage_map_errno(EEXIST));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, storage_map_errno(EINVAL));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, storage_map_errno(ENAMETOOLONG));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, storage_map_errno(EIO));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, storage_map_errno(EBADF));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_names);
    RUN_TEST(test_rejected_names);
    RUN_TEST(test_paths);
    RUN_TEST(test_path_errors);
    RUN_TEST(test_errno_mapping);
    return UNITY_END();
}
