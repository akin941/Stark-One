/*
 * test_storage_core.c — the storage mount state machine (TASKS.md
 * STARK-0201): every state x input, retry/probe cadence, clock wrap.
 */
#include <string.h>
#include "storage_core.h"
#include "unity.h"

static const storage_timing_t T = {3000, 1000};
static storage_core_t c;

void setUp(void)
{
    storage_core_init(&c, true, T, 100);
}
void tearDown(void)
{}

static storage_step_t tick(uint32_t now)
{
    return storage_core_step(&c, STORAGE_IN_TICK, STARK_OK, now);
}

static storage_step_t mount_result(stark_err_t err, uint32_t now)
{
    return storage_core_step(&c, STORAGE_IN_MOUNT_RESULT, err, now);
}

static storage_step_t probe_result(stark_err_t err, uint32_t now)
{
    return storage_core_step(&c, STORAGE_IN_PROBE_RESULT, err, now);
}

static void assert_step(storage_action_t action, bool changed, storage_step_t s)
{
    TEST_ASSERT_EQUAL_INT(action, s.action);
    TEST_ASSERT_EQUAL(changed, s.changed);
}

static void mount_ok_at(uint32_t now)
{
    assert_step(STORAGE_ACT_TRY_MOUNT, false, tick(now));
    assert_step(STORAGE_ACT_NONE, true, mount_result(STARK_OK, now));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_MOUNTED, c.state);
}

static void test_not_fitted_is_absorbing(void)
{
    storage_core_init(&c, false, T, 0);
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_NOT_FITTED, c.state);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, storage_core_wait_ms(&c, 0));
    assert_step(STORAGE_ACT_NONE, false, tick(0));
    assert_step(STORAGE_ACT_NONE, false, mount_result(STARK_OK, 0));
    assert_step(STORAGE_ACT_NONE, false, probe_result(STARK_ERR_NOT_FOUND, 0));
    assert_step(STORAGE_ACT_NONE, false, storage_core_step(&c, STORAGE_IN_IO_ERROR, STARK_OK, 0));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_NOT_FITTED, c.state);
}

static void test_first_attempt_is_immediate_and_announced(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, storage_core_wait_ms(&c, 100));
    assert_step(STORAGE_ACT_TRY_MOUNT, false, tick(100));
    /* ABSENT is also the start state, yet the first result is announced */
    assert_step(STORAGE_ACT_NONE, true, mount_result(STARK_ERR_NOT_FOUND, 120));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_ABSENT, c.state);
}

static void test_retries_are_silent_and_paced(void)
{
    (void)tick(100);
    (void)mount_result(STARK_ERR_NOT_FOUND, 100);
    TEST_ASSERT_EQUAL_UINT32(3000, storage_core_wait_ms(&c, 100));
    assert_step(STORAGE_ACT_NONE, false, tick(3099)); /* not yet */
    TEST_ASSERT_EQUAL_UINT32(1, storage_core_wait_ms(&c, 3099));
    assert_step(STORAGE_ACT_TRY_MOUNT, false, tick(3100));
    assert_step(STORAGE_ACT_NONE, false, mount_result(STARK_ERR_NOT_FOUND, 3100));
    assert_step(STORAGE_ACT_TRY_MOUNT, false, tick(6100));
}

static void test_mount_then_probe_cadence(void)
{
    mount_ok_at(100);
    TEST_ASSERT_EQUAL_UINT32(1000, storage_core_wait_ms(&c, 100));
    assert_step(STORAGE_ACT_NONE, false, tick(1099));
    assert_step(STORAGE_ACT_PROBE, false, tick(1100));
    assert_step(STORAGE_ACT_NONE, false, probe_result(STARK_OK, 1100));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_MOUNTED, c.state);
    assert_step(STORAGE_ACT_PROBE, false, tick(2100));
}

static void test_probe_failure_removes_and_unmounts(void)
{
    mount_ok_at(100);
    (void)tick(1100);
    uint32_t gen = c.generation;
    assert_step(STORAGE_ACT_UNMOUNT, true, probe_result(STARK_ERR_NOT_FOUND, 1100));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_REMOVED, c.state);
    TEST_ASSERT_EQUAL_UINT32(gen + 1, c.generation);
    /* retries resume at the retry cadence; failures keep REMOVED, silently */
    assert_step(STORAGE_ACT_NONE, false, tick(4099));
    assert_step(STORAGE_ACT_TRY_MOUNT, false, tick(4100));
    assert_step(STORAGE_ACT_NONE, false, mount_result(STARK_ERR_NOT_FOUND, 4100));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_REMOVED, c.state);
    /* reinserted */
    assert_step(STORAGE_ACT_TRY_MOUNT, false, tick(7100));
    assert_step(STORAGE_ACT_NONE, true, mount_result(STARK_OK, 7100));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_MOUNTED, c.state);
}

static void test_probe_io_error_also_removes(void)
{
    mount_ok_at(100);
    (void)tick(1100);
    assert_step(STORAGE_ACT_UNMOUNT, true, probe_result(STARK_ERR_IO, 1100));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_REMOVED, c.state);
}

static void test_removed_then_unusable(void)
{
    mount_ok_at(100);
    (void)tick(1100);
    (void)probe_result(STARK_ERR_NOT_FOUND, 1100);
    (void)tick(4100);
    assert_step(STORAGE_ACT_NONE, true, mount_result(STARK_ERR_NOT_SUPPORTED, 4100));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_UNUSABLE, c.state);
}

static void test_unusable_card_and_its_removal(void)
{
    (void)tick(100);
    assert_step(STORAGE_ACT_NONE, true, mount_result(STARK_ERR_NOT_SUPPORTED, 100));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_UNUSABLE, c.state);
    assert_step(STORAGE_ACT_TRY_MOUNT, false, tick(3100)); /* unusable cards are retried */
    assert_step(STORAGE_ACT_NONE, false, mount_result(STARK_ERR_NOT_SUPPORTED, 3100));
    assert_step(STORAGE_ACT_TRY_MOUNT, false, tick(6100));
    assert_step(STORAGE_ACT_NONE, true, mount_result(STARK_ERR_NOT_FOUND, 6100)); /* pulled */
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_ABSENT, c.state);
}

static void test_mount_io_error_is_unusable(void)
{
    (void)tick(100);
    assert_step(STORAGE_ACT_NONE, true, mount_result(STARK_ERR_IO, 100));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_UNUSABLE, c.state);
}

static void test_io_error_triggers_an_immediate_probe(void)
{
    mount_ok_at(100);
    assert_step(STORAGE_ACT_NONE, false, storage_core_step(&c, STORAGE_IN_IO_ERROR, STARK_OK, 200));
    TEST_ASSERT_EQUAL_UINT32(0, storage_core_wait_ms(&c, 200));
    assert_step(STORAGE_ACT_PROBE, false, tick(200));
    TEST_ASSERT_EQUAL_UINT32(1000, storage_core_wait_ms(&c, 200)); /* cadence restarts */
}

static void test_io_error_when_not_mounted_is_ignored(void)
{
    (void)tick(100);
    (void)mount_result(STARK_ERR_NOT_FOUND, 100);
    assert_step(STORAGE_ACT_NONE, false, storage_core_step(&c, STORAGE_IN_IO_ERROR, STARK_OK, 200));
    TEST_ASSERT_EQUAL_UINT32(2900, storage_core_wait_ms(&c, 200));
}

static void test_clock_wrap(void)
{
    uint32_t near = UINT32_MAX - 500u;
    storage_core_init(&c, true, T, near);
    (void)tick(near);
    (void)mount_result(STARK_ERR_NOT_FOUND, near);
    uint32_t after = near + 2999u; /* wrapped past zero */
    TEST_ASSERT_TRUE(after < near);
    TEST_ASSERT_EQUAL_UINT32(1, storage_core_wait_ms(&c, after));
    assert_step(STORAGE_ACT_NONE, false, tick(after));
    assert_step(STORAGE_ACT_TRY_MOUNT, false, tick(after + 1u));
}

static storage_step_t req(storage_input_t in, uint32_t now)
{
    return storage_core_step(&c, in, STARK_OK, now);
}

static storage_step_t present_result(stark_err_t err, uint32_t now)
{
    return storage_core_step(&c, STORAGE_IN_PRESENT_RESULT, err, now);
}

static void test_eject_unmounts_and_bumps_generation(void)
{
    mount_ok_at(100);
    uint32_t gen = c.generation;
    assert_step(STORAGE_ACT_UNMOUNT, true, req(STORAGE_IN_REQ_EJECT, 500));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_EJECTED, c.state);
    TEST_ASSERT_EQUAL_UINT32(gen + 1, c.generation);
    TEST_ASSERT_EQUAL_UINT32(3000, storage_core_wait_ms(&c, 500));
}

static void test_eject_ignored_unless_mounted(void)
{
    (void)tick(100);
    (void)mount_result(STARK_ERR_NOT_FOUND, 100);
    assert_step(STORAGE_ACT_NONE, false, req(STORAGE_IN_REQ_EJECT, 200));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_ABSENT, c.state);
}

static void test_ejected_never_mounts_by_itself(void)
{
    mount_ok_at(100);
    (void)req(STORAGE_IN_REQ_EJECT, 500);
    /* a card that still answers stays ejected, silently, check after check */
    assert_step(STORAGE_ACT_PRESENT_CHECK, false, tick(3500));
    assert_step(STORAGE_ACT_NONE, false, present_result(STARK_OK, 3500));
    assert_step(STORAGE_ACT_PRESENT_CHECK, false, tick(6500));
    assert_step(STORAGE_ACT_NONE, false, present_result(STARK_OK, 6500));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_EJECTED, c.state);
}

static void test_ejected_then_pulled_then_inserted(void)
{
    mount_ok_at(100);
    (void)req(STORAGE_IN_REQ_EJECT, 500);
    (void)tick(3500);
    assert_step(STORAGE_ACT_NONE, true, present_result(STARK_ERR_NOT_FOUND, 3500));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_ABSENT, c.state);
    assert_step(STORAGE_ACT_TRY_MOUNT, false, tick(6500));
    assert_step(STORAGE_ACT_NONE, true, mount_result(STARK_OK, 6500));
}

static void test_present_result_outside_ejected_is_ignored(void)
{
    mount_ok_at(100);
    assert_step(STORAGE_ACT_NONE, false, present_result(STARK_ERR_NOT_FOUND, 200));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_MOUNTED, c.state);
}

static void test_request_mount(void)
{
    /* absent: tries at once instead of waiting for the retry */
    (void)tick(100);
    (void)mount_result(STARK_ERR_NOT_FOUND, 100);
    assert_step(STORAGE_ACT_TRY_MOUNT, false, req(STORAGE_IN_REQ_MOUNT, 200));
    assert_step(STORAGE_ACT_NONE, true, mount_result(STARK_OK, 200));
    /* mounted: nothing to do */
    assert_step(STORAGE_ACT_NONE, false, req(STORAGE_IN_REQ_MOUNT, 300));
    /* ejected: mounts at once; a card that is gone by then is absent */
    (void)req(STORAGE_IN_REQ_EJECT, 400);
    assert_step(STORAGE_ACT_TRY_MOUNT, false, req(STORAGE_IN_REQ_MOUNT, 500));
    assert_step(STORAGE_ACT_NONE, true, mount_result(STARK_ERR_NOT_FOUND, 500));
    TEST_ASSERT_EQUAL_INT(STARK_STORAGE_ABSENT, c.state);
}

static void test_unknown_input_is_ignored(void)
{
    assert_step(STORAGE_ACT_NONE, false, storage_core_step(&c, (storage_input_t)99, STARK_OK, 100));
}

static void test_state_names(void)
{
    TEST_ASSERT_EQUAL_STRING("not fitted", stark_storage_state_str(STARK_STORAGE_NOT_FITTED));
    TEST_ASSERT_EQUAL_STRING("no card", stark_storage_state_str(STARK_STORAGE_ABSENT));
    TEST_ASSERT_EQUAL_STRING("mounted", stark_storage_state_str(STARK_STORAGE_MOUNTED));
    TEST_ASSERT_EQUAL_STRING("unusable", stark_storage_state_str(STARK_STORAGE_UNUSABLE));
    TEST_ASSERT_EQUAL_STRING("removed", stark_storage_state_str(STARK_STORAGE_REMOVED));
    TEST_ASSERT_EQUAL_STRING("ejected", stark_storage_state_str(STARK_STORAGE_EJECTED));
    TEST_ASSERT_EQUAL_STRING("?", stark_storage_state_str((stark_storage_state_t)42));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_not_fitted_is_absorbing);
    RUN_TEST(test_first_attempt_is_immediate_and_announced);
    RUN_TEST(test_retries_are_silent_and_paced);
    RUN_TEST(test_mount_then_probe_cadence);
    RUN_TEST(test_probe_failure_removes_and_unmounts);
    RUN_TEST(test_probe_io_error_also_removes);
    RUN_TEST(test_removed_then_unusable);
    RUN_TEST(test_unusable_card_and_its_removal);
    RUN_TEST(test_mount_io_error_is_unusable);
    RUN_TEST(test_io_error_triggers_an_immediate_probe);
    RUN_TEST(test_io_error_when_not_mounted_is_ignored);
    RUN_TEST(test_clock_wrap);
    RUN_TEST(test_eject_unmounts_and_bumps_generation);
    RUN_TEST(test_eject_ignored_unless_mounted);
    RUN_TEST(test_ejected_never_mounts_by_itself);
    RUN_TEST(test_ejected_then_pulled_then_inserted);
    RUN_TEST(test_present_result_outside_ejected_is_ignored);
    RUN_TEST(test_request_mount);
    RUN_TEST(test_unknown_input_is_ignored);
    RUN_TEST(test_state_names);
    return UNITY_END();
}
