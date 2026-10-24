/*
 * test_buzzer_volume.c — volume level -> duty (TASKS.md STARK-0204).
 */
#include "buzzer_volume.h"
#include "unity.h"

void setUp(void)
{}
void tearDown(void)
{}

static void test_duty_table(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, buzzer_volume_duty(0)); /* Off */
    TEST_ASSERT_EQUAL_UINT8(5, buzzer_volume_duty(1));
    TEST_ASSERT_EQUAL_UINT8(15, buzzer_volume_duty(2));
    TEST_ASSERT_EQUAL_UINT8(50, buzzer_volume_duty(3)); /* the V0 level */
    TEST_ASSERT_EQUAL_UINT8(0, buzzer_volume_duty(4));
    TEST_ASSERT_EQUAL_UINT8(0, buzzer_volume_duty(255));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_duty_table);
    return UNITY_END();
}
