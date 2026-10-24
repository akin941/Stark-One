/*
 * test_settings_core.c — settings table, validation, load/set/commit over a
 * fake NVS with every failure switch (TASKS.md STARK-0204).
 */
#include <string.h>
#include "fake_nvs.h"
#include "settings_core.h"
#include "unity.h"

static settings_core_t c;
static int s_changes, s_invalid;
static stark_setting_t s_last_key;
static uint8_t s_last_value;

static void changed(stark_setting_t k, uint8_t v, void *ctx)
{
    (void)ctx;
    s_changes++;
    s_last_key = k;
    s_last_value = v;
}

static void invalid(stark_setting_t k, uint8_t v, void *ctx)
{
    (void)ctx;
    (void)k;
    (void)v;
    s_invalid++;
}

static const settings_hooks_t H = {changed, invalid, NULL};

void setUp(void)
{
    fake_nvs_reset();
    s_changes = s_invalid = 0;
}
void tearDown(void)
{}

static void assert_defaults(void)
{
    TEST_ASSERT_EQUAL_UINT8(100, c.value[STARK_SET_BRIGHTNESS]);
    TEST_ASSERT_EQUAL_UINT8(3, c.value[STARK_SET_VOLUME]);
    TEST_ASSERT_EQUAL_UINT8(1, c.value[STARK_SET_REPEAT]);
    TEST_ASSERT_EQUAL_UINT8(2, c.value[STARK_SET_LOG_LEVEL]);
}

static void test_table(void)
{
    TEST_ASSERT_EQUAL_STRING("brightness", settings_desc(STARK_SET_BRIGHTNESS)->name);
    TEST_ASSERT_EQUAL_STRING("bright", settings_desc(STARK_SET_BRIGHTNESS)->key);
    TEST_ASSERT_NULL(settings_desc(STARK_SET_BRIGHTNESS)->labels);
    TEST_ASSERT_EQUAL_STRING("High", settings_desc(STARK_SET_VOLUME)->labels[3]);
    TEST_ASSERT_EQUAL_STRING("Fast", settings_desc(STARK_SET_REPEAT)->labels[2]);
    TEST_ASSERT_EQUAL_STRING("Debug", settings_desc(STARK_SET_LOG_LEVEL)->labels[3]);
    TEST_ASSERT_NULL(settings_desc(STARK_SET_COUNT));
    for (int k = 0; k < STARK_SET_COUNT; k++) {
        TEST_ASSERT_TRUE(strlen(settings_desc((stark_setting_t)k)->key) <= 15);
    }
}

static void test_validation(void)
{
    TEST_ASSERT_TRUE(settings_valid(STARK_SET_BRIGHTNESS, 10));
    TEST_ASSERT_TRUE(settings_valid(STARK_SET_BRIGHTNESS, 70));
    TEST_ASSERT_FALSE(settings_valid(STARK_SET_BRIGHTNESS, 75)); /* off-step */
    TEST_ASSERT_FALSE(settings_valid(STARK_SET_BRIGHTNESS, 0));
    TEST_ASSERT_FALSE(settings_valid(STARK_SET_BRIGHTNESS, 110));
    TEST_ASSERT_TRUE(settings_valid(STARK_SET_VOLUME, 0));
    TEST_ASSERT_FALSE(settings_valid(STARK_SET_VOLUME, 4));
    TEST_ASSERT_FALSE(settings_valid(STARK_SET_COUNT, 1));
}

static void test_fresh_store_writes_schema(void)
{
    settings_core_load(&c, &fake_nvs, &H);
    TEST_ASSERT_EQUAL(SETTINGS_FROM_DEFAULTS, c.source);
    assert_defaults();
    uint8_t v = 0;
    TEST_ASSERT_TRUE(fake_nvs_value("schema", &v));
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_SCHEMA, v);
    TEST_ASSERT_FALSE(fake_nvs_value("bright", &v)); /* defaults are not written */
    TEST_ASSERT_EQUAL(0, s_changes);
    /* second boot: from NVS */
    settings_core_load(&c, &fake_nvs, &H);
    TEST_ASSERT_EQUAL(SETTINGS_FROM_NVS, c.source);
    assert_defaults();
}

static void test_round_trip(void)
{
    settings_core_load(&c, &fake_nvs, &H);
    TEST_ASSERT_EQUAL(STARK_OK, settings_core_set(&c, STARK_SET_BRIGHTNESS, 80, &H));
    TEST_ASSERT_EQUAL(STARK_OK, settings_core_set(&c, STARK_SET_VOLUME, 2, &H));
    TEST_ASSERT_EQUAL(2, s_changes);
    TEST_ASSERT_EQUAL(STARK_SET_VOLUME, s_last_key);
    TEST_ASSERT_EQUAL_UINT8(2, s_last_value);
    uint32_t n = 0;
    int writes = fake_nvs_writes();
    TEST_ASSERT_EQUAL(STARK_OK, settings_core_commit(&c, &n));
    TEST_ASSERT_EQUAL_UINT32(2, n);
    TEST_ASSERT_EQUAL(writes + 2, fake_nvs_writes());          /* only the dirty keys */
    TEST_ASSERT_EQUAL(STARK_OK, settings_core_commit(&c, &n)); /* nothing dirty */
    TEST_ASSERT_EQUAL_UINT32(0, n);
    TEST_ASSERT_EQUAL(writes + 2, fake_nvs_writes());
    settings_core_load(&c, &fake_nvs, &H); /* reboot */
    TEST_ASSERT_EQUAL_UINT8(80, c.value[STARK_SET_BRIGHTNESS]);
    TEST_ASSERT_EQUAL_UINT8(2, c.value[STARK_SET_VOLUME]);
}

static void test_set_rejections_and_no_op(void)
{
    settings_core_load(&c, &fake_nvs, &H);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, settings_core_set(&c, STARK_SET_BRIGHTNESS, 55, &H));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, settings_core_set(&c, STARK_SET_REPEAT, 3, &H));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, settings_core_set(&c, STARK_SET_COUNT, 0, &H));
    TEST_ASSERT_EQUAL(STARK_OK, settings_core_set(&c, STARK_SET_REPEAT, 1, &H)); /* unchanged */
    TEST_ASSERT_EQUAL(0, s_changes);
    TEST_ASSERT_FALSE(c.dirty[STARK_SET_REPEAT]);
    TEST_ASSERT_EQUAL(STARK_OK, settings_core_set(&c, STARK_SET_REPEAT, 2, NULL)); /* no hooks */
}

static void test_invalid_stored_values(void)
{
    fake_nvs_put("schema", 1);
    fake_nvs_put("bright", 55); /* off-step */
    fake_nvs_put("volume", 9);  /* out of range */
    fake_nvs_put("repeat", 2);
    fake_nvs_fail_get("loglvl", STARK_ERR_IO); /* e.g. a type mismatch */
    settings_core_load(&c, &fake_nvs, &H);
    TEST_ASSERT_EQUAL(SETTINGS_FROM_NVS, c.source);
    TEST_ASSERT_EQUAL(3, s_invalid);
    TEST_ASSERT_EQUAL_UINT8(100, c.value[STARK_SET_BRIGHTNESS]);
    TEST_ASSERT_EQUAL_UINT8(3, c.value[STARK_SET_VOLUME]);
    TEST_ASSERT_EQUAL_UINT8(2, c.value[STARK_SET_REPEAT]);
    TEST_ASSERT_EQUAL_UINT8(2, c.value[STARK_SET_LOG_LEVEL]);
    fake_nvs_fail_get(NULL, STARK_OK);
    uint32_t n = 0;
    TEST_ASSERT_EQUAL(STARK_OK, settings_core_commit(&c, &n)); /* rewritten with defaults */
    TEST_ASSERT_EQUAL_UINT32(3, n);
    uint8_t v = 0;
    TEST_ASSERT_TRUE(fake_nvs_value("bright", &v));
    TEST_ASSERT_EQUAL_UINT8(100, v);
    /* a newer schema: values are still validated, not trusted */
    fake_nvs_put("schema", 7);
    fake_nvs_put("bright", 5);
    settings_core_load(&c, &fake_nvs, NULL); /* no hooks: still replaced */
    TEST_ASSERT_EQUAL_UINT8(100, c.value[STARK_SET_BRIGHTNESS]);
    TEST_ASSERT_TRUE(c.dirty[STARK_SET_BRIGHTNESS]);
}

static void test_unreadable_store_is_ram_only(void)
{
    fake_nvs_fail_get("schema", STARK_ERR_IO);
    settings_core_load(&c, &fake_nvs, &H);
    TEST_ASSERT_EQUAL(SETTINGS_FROM_RAM, c.source);
    assert_defaults();
    TEST_ASSERT_EQUAL(STARK_OK, settings_core_set(&c, STARK_SET_VOLUME, 0, &H)); /* RAM works */
    uint32_t n = 9;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, settings_core_commit(&c, &n));
    TEST_ASSERT_EQUAL_UINT32(0, n);
    /* no store at all (nvs_open failed) */
    settings_core_load(&c, NULL, &H);
    TEST_ASSERT_EQUAL(SETTINGS_FROM_RAM, c.source);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, settings_core_commit(&c, &n));
}

static void test_write_failures_keep_dirty(void)
{
    settings_core_load(&c, &fake_nvs, &H);
    (void)settings_core_set(&c, STARK_SET_REPEAT, 2, &H);
    uint32_t n = 0;
    fake_nvs_fail_set(STARK_ERR_IO);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, settings_core_commit(&c, &n));
    TEST_ASSERT_TRUE(c.dirty[STARK_SET_REPEAT]);
    fake_nvs_fail_set(STARK_OK);
    fake_nvs_fail_commit(STARK_ERR_IO);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, settings_core_commit(&c, &n));
    TEST_ASSERT_TRUE(c.dirty[STARK_SET_REPEAT]);
    fake_nvs_fail_commit(STARK_OK);
    TEST_ASSERT_EQUAL(STARK_OK, settings_core_commit(&c, &n)); /* retried */
    TEST_ASSERT_EQUAL_UINT32(1, n);
    uint8_t v = 0;
    TEST_ASSERT_TRUE(fake_nvs_value("repeat", &v));
    TEST_ASSERT_EQUAL_UINT8(2, v);
}

static void test_schema_write_failure_on_fresh_store(void)
{
    fake_nvs_fail_set(STARK_ERR_IO);
    settings_core_load(&c, &fake_nvs, &H);
    TEST_ASSERT_EQUAL(SETTINGS_FROM_DEFAULTS, c.source);
    TEST_ASSERT_TRUE(c.schema_dirty); /* retried at the next commit */
    fake_nvs_fail_set(STARK_OK);
    uint32_t n = 0;
    TEST_ASSERT_EQUAL(STARK_OK, settings_core_commit(&c, &n));
    TEST_ASSERT_EQUAL_UINT32(0, n); /* only the schema */
    uint8_t v = 0;
    TEST_ASSERT_TRUE(fake_nvs_value("schema", &v));
}

static void test_restore_defaults(void)
{
    settings_core_load(&c, &fake_nvs, &H);
    (void)settings_core_set(&c, STARK_SET_BRIGHTNESS, 30, &H);
    (void)settings_core_set(&c, STARK_SET_LOG_LEVEL, 0, &H);
    uint32_t n = 0;
    (void)settings_core_commit(&c, &n);
    s_changes = 0;
    settings_core_restore(&c, &H);
    TEST_ASSERT_EQUAL(2, s_changes); /* only the two that differed */
    assert_defaults();
    TEST_ASSERT_EQUAL(STARK_OK, settings_core_commit(&c, &n));
    TEST_ASSERT_EQUAL_UINT32(2, n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_table);
    RUN_TEST(test_validation);
    RUN_TEST(test_fresh_store_writes_schema);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_set_rejections_and_no_op);
    RUN_TEST(test_invalid_stored_values);
    RUN_TEST(test_unreadable_store_is_ram_only);
    RUN_TEST(test_write_failures_keep_dirty);
    RUN_TEST(test_schema_write_failure_on_fresh_store);
    RUN_TEST(test_restore_defaults);
    return UNITY_END();
}
