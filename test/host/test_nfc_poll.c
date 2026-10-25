/*
 * test_nfc_poll.c — the poll state machine (TASKS.md STARK-0403): arrival,
 * the same tag again, removal debounce, flapping, a tag replaced without a
 * gap, module loss and recovery.
 */
#include <string.h>
#include "nfc_poll.h"
#include "unity.h"

static nfc_poll_t s_p;
static nfc_tag_t s_a, s_b;

void setUp(void)
{
    nfc_poll_init(&s_p);
    memset(&s_a, 0, sizeof s_a);
    memset(&s_b, 0, sizeof s_b);
    s_a.uid_len = 7;
    memcpy(s_a.uid, (const uint8_t[]){4, 1, 2, 3, 4, 5, 6}, 7);
    s_b.uid_len = 4;
    memcpy(s_b.uid, (const uint8_t[]){0xDE, 0xAD, 0xBE, 0xEF}, 4);
}
void tearDown(void)
{}

static nfc_poll_event_t tag(const nfc_tag_t *t)
{
    return nfc_poll_result(&s_p, STARK_OK, true, t, 0);
}

static nfc_poll_event_t empty(void)
{
    return nfc_poll_result(&s_p, STARK_OK, false, NULL, 0);
}

static nfc_poll_event_t error(uint32_t now)
{
    return nfc_poll_result(&s_p, STARK_ERR_IO, false, NULL, now);
}

static void test_arrival_repeat_and_leave(void)
{
    TEST_ASSERT_EQUAL(NFC_POLL_NONE, empty());
    TEST_ASSERT_EQUAL(NFC_POLL_TAG, tag(&s_a));
    TEST_ASSERT_EQUAL(NFC_POLL_PRESENT, s_p.state);
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_EQUAL(NFC_POLL_NONE, tag(&s_a)); /* read once, not every poll */
    }
    TEST_ASSERT_EQUAL(NFC_POLL_NONE, empty()); /* one miss is not a removal */
    TEST_ASSERT_EQUAL(NFC_POLL_LEFT, empty());
    TEST_ASSERT_EQUAL(NFC_POLL_IDLE, s_p.state);
    TEST_ASSERT_EQUAL(NFC_POLL_NONE, empty());
    TEST_ASSERT_EQUAL(NFC_POLL_TAG, tag(&s_a)); /* back: a new arrival */
}

static void test_flapping(void)
{
    TEST_ASSERT_EQUAL(NFC_POLL_TAG, tag(&s_a));
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL(NFC_POLL_NONE, empty());
        TEST_ASSERT_EQUAL(NFC_POLL_NONE, tag(&s_a)); /* a miss then back: still the same tag */
    }
}

static void test_replaced_without_a_gap(void)
{
    TEST_ASSERT_EQUAL(NFC_POLL_TAG, tag(&s_a));
    TEST_ASSERT_EQUAL(NFC_POLL_TAG, tag(&s_b));
    TEST_ASSERT_EQUAL(NFC_POLL_NONE, tag(&s_b));
    s_b.uid_len = 7; /* same length as s_a, different bytes */
    memcpy(s_b.uid, (const uint8_t[]){4, 1, 2, 3, 4, 5, 7}, 7);
    TEST_ASSERT_EQUAL(NFC_POLL_TAG, tag(&s_b));
}

static void test_module_lost_and_back(void)
{
    TEST_ASSERT_EQUAL(NFC_POLL_TAG, tag(&s_a));
    TEST_ASSERT_EQUAL(NFC_POLL_NONE, error(100));
    TEST_ASSERT_EQUAL(NFC_POLL_NONE, tag(&s_a)); /* an error in between resets the count */
    TEST_ASSERT_EQUAL(NFC_POLL_NONE, error(200));
    TEST_ASSERT_EQUAL(NFC_POLL_NONE, error(300));
    TEST_ASSERT_EQUAL(NFC_POLL_LOST, error(400));
    TEST_ASSERT_EQUAL(NFC_POLL_RECOVERING, s_p.state);
    TEST_ASSERT_EQUAL(NFC_POLL_NONE, tag(&s_a)); /* polls are ignored while recovering */
    TEST_ASSERT_FALSE(nfc_poll_recover_due(&s_p, 400 + NFC_POLL_RECOVER_MS - 1));
    TEST_ASSERT_TRUE(nfc_poll_recover_due(&s_p, 400 + NFC_POLL_RECOVER_MS));
    TEST_ASSERT_EQUAL(NFC_POLL_NONE, nfc_poll_recovered(&s_p, STARK_ERR_NOT_FOUND, 1400));
    TEST_ASSERT_FALSE(nfc_poll_recover_due(&s_p, 1400 + NFC_POLL_RECOVER_MS - 1));
    TEST_ASSERT_EQUAL(NFC_POLL_BACK, nfc_poll_recovered(&s_p, STARK_OK, 2400));
    TEST_ASSERT_EQUAL(NFC_POLL_IDLE, s_p.state);
    TEST_ASSERT_EQUAL(NFC_POLL_TAG, tag(&s_a)); /* the tag is read again after recovery */
    TEST_ASSERT_EQUAL(NFC_POLL_NONE, nfc_poll_recovered(&s_p, STARK_OK, 2500)); /* not recovering */
    TEST_ASSERT_FALSE(nfc_poll_recover_due(&s_p, 9999));
}

static void test_recover_due_wraps(void)
{
    for (int i = 0; i < 3; i++) {
        (void)error(UINT32_MAX - 100);
    }
    TEST_ASSERT_TRUE(nfc_poll_recover_due(&s_p, UINT32_MAX - 100 + NFC_POLL_RECOVER_MS));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_arrival_repeat_and_leave);
    RUN_TEST(test_flapping);
    RUN_TEST(test_replaced_without_a_gap);
    RUN_TEST(test_module_lost_and_back);
    RUN_TEST(test_recover_due_wraps);
    return UNITY_END();
}
