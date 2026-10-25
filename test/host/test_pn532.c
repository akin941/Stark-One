/*
 * test_pn532.c — the production PN532 driver against the fake PN532
 * (TASKS.md STARK-0402, ADR-0020 §3): every framing, ACK, checksum, timeout,
 * bus and recovery path, through the transport seam. Time is the test's: it
 * moves only when the driver waits, so nothing sleeps.
 */
#include <string.h>
#include "fake_pn532.h"
#include "pn532.h"
#include "unity.h"

static uint32_t s_now;
static int s_writes, s_reads;   /* transport calls so far */
static int s_write_err_at;      /* 1-based write that fails (0: none) ... */
static bool s_write_err_onward; /* ... and every write after it */
static int s_read_err_at;       /* 1-based read that fails with s_read_err (0: none) */
static stark_err_t s_read_err;
static int s_not_ready_at; /* a full read that reports "not ready" despite the poll */

static stark_err_t io_write(void *ctx, const uint8_t *buf, size_t len)
{
    (void)ctx;
    s_writes++;
    if (s_write_err_at > 0 &&
        (s_writes == s_write_err_at || (s_write_err_onward && s_writes > s_write_err_at))) {
        return STARK_ERR_IO;
    }
    return fake_pn532_write(buf, len);
}

static stark_err_t io_read(void *ctx, uint8_t *buf, size_t len)
{
    (void)ctx;
    if (++s_reads == s_read_err_at) {
        return s_read_err;
    }
    stark_err_t err = fake_pn532_read(buf, len);
    if (len > 1 && s_not_ready_at > 0 && --s_not_ready_at == 0) {
        buf[0] = 0x00; /* this read missed the frame */
    }
    return err;
}

static uint32_t io_now(void *ctx)
{
    (void)ctx;
    return s_now;
}

static void io_delay(void *ctx, uint32_t ms)
{
    (void)ctx;
    s_now += ms;
}

static const pn532_io_t k_io = {io_write, io_read, io_now, io_delay, NULL};
static pn532_t s_d;

void setUp(void)
{
    fake_pn532_reset();
    s_now = 1000;
    s_write_err_at = s_read_err_at = s_not_ready_at = 0;
    s_write_err_onward = false;
    s_read_err = STARK_ERR_IO;
    s_writes = s_reads = 0;
    pn532_bind(&s_d, &k_io);
}
void tearDown(void)
{}

static stark_err_t firmware(uint8_t *ic)
{
    uint8_t ver, rev;
    return pn532_firmware(&s_d, ic, &ver, &rev);
}

static void test_valid_ack_and_response(void)
{
    uint8_t ic, ver, rev;
    TEST_ASSERT_EQUAL(STARK_OK, pn532_firmware(&s_d, &ic, &ver, &rev));
    TEST_ASSERT_EQUAL_HEX8(0x32, ic);
    TEST_ASSERT_EQUAL(1, ver);
    TEST_ASSERT_EQUAL(6, rev);
    pn532_stats_t zero = {0};
    TEST_ASSERT_EQUAL_MEMORY(&zero, &s_d.stats, sizeof zero);
    TEST_ASSERT_EQUAL(1, fake_pn532_stats()->frames);
    TEST_ASSERT_EQUAL(0, fake_pn532_stats()->bad_frames); /* the driver's frames are valid */
}

static void test_nack_retried(void)
{
    uint8_t ic;
    fake_pn532_fault(FAKE_SEND_NACK, 1);
    TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic));
    TEST_ASSERT_EQUAL(1, s_d.stats.retries);
    fake_pn532_fault(FAKE_SEND_NACK, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, firmware(&ic));
    TEST_ASSERT_EQUAL(1 + PN532_RETRIES, s_d.stats.retries);
}

static void test_bad_ack_retried(void)
{
    uint8_t ic;
    fake_pn532_fault(FAKE_BAD_ACK, 1);
    TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic));
    TEST_ASSERT_EQUAL(1, s_d.stats.retries);
}

static void test_missing_ack(void)
{
    uint8_t ic;
    fake_pn532_fault(FAKE_SILENT, 1);
    TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic));
    TEST_ASSERT_EQUAL(1, s_d.stats.timeouts);
    fake_pn532_fault(FAKE_SILENT, -1);
    uint32_t t0 = s_now;
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, firmware(&ic));
    TEST_ASSERT_GREATER_OR_EQUAL((PN532_RETRIES + 1) * PN532_ACK_TIMEOUT_MS, s_now - t0);
}

static void test_checksum_error_resent(void)
{
    uint8_t ic;
    fake_pn532_fault(FAKE_BAD_LCS, 1);
    TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic));
    TEST_ASSERT_EQUAL(1, s_d.stats.naks_sent);
    TEST_ASSERT_EQUAL(1, s_d.stats.checksum_errors);
    TEST_ASSERT_EQUAL(1, fake_pn532_stats()->host_nacks);
    fake_pn532_fault(FAKE_BAD_DCS, 1);
    TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic));
    TEST_ASSERT_EQUAL(2, s_d.stats.checksum_errors);
}

static void test_persistent_checksum_error(void)
{
    uint8_t ic;
    fake_pn532_fault(FAKE_BAD_DCS, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, firmware(&ic));
    TEST_ASSERT_EQUAL(PN532_RESENDS, s_d.stats.naks_sent);
    TEST_ASSERT_EQUAL(PN532_RESENDS + 1, s_d.stats.checksum_errors);
}

static void test_truncated_response(void)
{
    uint8_t ic;
    fake_pn532_fault(FAKE_TRUNCATED, 1);
    TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic)); /* resent whole */
    TEST_ASSERT_EQUAL(1, s_d.stats.naks_sent);
    /* an I2C read has a fixed length: the missing tail reads as zeros, so a cut
     * frame fails its data checksum rather than looking short */
    TEST_ASSERT_EQUAL(1, s_d.stats.checksum_errors);
    fake_pn532_fault(FAKE_TRUNCATED, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, firmware(&ic));
}

static void test_never_ready_times_out_and_aborts(void)
{
    uint8_t ic;
    fake_pn532_fault(FAKE_NEVER_READY, 1);
    uint32_t t0 = s_now;
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, firmware(&ic));
    TEST_ASSERT_GREATER_OR_EQUAL(100, s_now - t0); /* the command's timeout, honoured */
    TEST_ASSERT_LESS_THAN(200, s_now - t0);
    TEST_ASSERT_EQUAL(1, fake_pn532_stats()->aborts);
    TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic)); /* recovery: the next command works */
    TEST_ASSERT_EQUAL_HEX8(0x32, ic);
}

/* Reads of one command: ACK poll, ACK frame, response poll (busy), response
 * poll (ready), response frame. */
static void test_bus_errors(void)
{
    uint8_t ic;
    fake_pn532_fault(FAKE_BUS_ERROR, 1); /* the command write */
    TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic));
    TEST_ASSERT_EQUAL(1, s_d.stats.bus_errors);
    TEST_ASSERT_EQUAL(1, s_d.stats.retries);
    fake_pn532_fault(FAKE_BUS_ERROR, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, firmware(&ic));
    fake_pn532_fault(FAKE_NONE, 0);

    s_read_err_at = s_reads + 1; /* the ACK poll: the command is repeated */
    TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic));
    s_read_err_at = s_reads + 2; /* the ACK frame read: repeated too */
    TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic));
    s_read_err_at = s_reads + 3; /* the response poll: reported */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, firmware(&ic));
    TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic)); /* the chip dropped it; the next one works */

    fake_pn532_fault(FAKE_BAD_DCS, 1);
    s_write_err_at = s_writes + 2; /* the host NACK asking for a resend */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, firmware(&ic));
}

static void test_no_device(void)
{
    uint8_t ic;
    fake_pn532_fault(FAKE_NO_DEVICE, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, firmware(&ic));
    TEST_ASSERT_EQUAL(0, s_d.stats.retries); /* nothing there: no point repeating */
    TEST_ASSERT_EQUAL(0, s_d.stats.bus_errors);
    fake_pn532_fault(FAKE_NONE, 0);
    TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic)); /* the module came back */
}

/* The write lands, then nothing answers when the ACK is polled. */
static void test_absent_during_poll(void)
{
    uint8_t ic;
    s_read_err_at = 1;
    s_read_err = STARK_ERR_NOT_FOUND;
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, firmware(&ic));
    TEST_ASSERT_EQUAL(0, s_d.stats.retries);
}

static void test_unexpected_responses(void)
{
    uint8_t ic;
    fake_pn532_fault(FAKE_WRONG_CODE, 1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, firmware(&ic));
    fake_pn532_fault(FAKE_APP_ERROR, 1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, firmware(&ic));
    fake_pn532_fault(FAKE_EXTENDED, 1);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, firmware(&ic));
    TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic)); /* and the chip is still usable */
}

static void test_missed_frame_on_full_read(void)
{
    uint8_t ic;
    s_not_ready_at = 1; /* the ACK's full read misses */
    TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic));
}

static void test_detect(void)
{
    uint8_t ic, ver, rev;
    TEST_ASSERT_EQUAL(STARK_OK, pn532_detect(&s_d, &ic, &ver, &rev));
    const fake_pn532_stats_t *st = fake_pn532_stats();
    TEST_ASSERT_FALSE(st->field);
    TEST_ASSERT_EQUAL(1, st->passive_retries);
    TEST_ASSERT_EQUAL_HEX8(0x32, st->last_cmd); /* RFConfiguration, last */
    TEST_ASSERT_EQUAL(STARK_OK, pn532_field(&s_d, true));
    TEST_ASSERT_TRUE(st->field);
    TEST_ASSERT_EQUAL(STARK_OK, pn532_field(&s_d, false));
    TEST_ASSERT_FALSE(st->field);

    fake_pn532_fault(FAKE_OTHER_IC, 1);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, pn532_detect(&s_d, &ic, &ver, &rev));
    TEST_ASSERT_EQUAL_HEX8(0x31, ic);
    fake_pn532_fault(FAKE_NO_DEVICE, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, pn532_detect(&s_d, &ic, &ver, &rev));
    fake_pn532_fault(FAKE_NONE, 0);
    TEST_ASSERT_EQUAL(STARK_OK, pn532_detect(&s_d, &ic, &ver, &rev)); /* re-initialised */
}

/* Detection's later steps (SAM, passive retries, field off) each report their
 * failure: every write from that command on fails. */
static void test_detect_step_failures(void)
{
    uint8_t ic, ver, rev;
    for (int step = 2; step <= 4; step++) {
        setUp();
        s_write_err_at = step;
        s_write_err_onward = true;
        TEST_ASSERT_EQUAL(STARK_ERR_IO, pn532_detect(&s_d, &ic, &ver, &rev));
        TEST_ASSERT_EQUAL_HEX8(0x32, ic); /* identified before the failure */
    }
}

static void test_repeated_queries_no_drift(void)
{
    uint8_t ic;
    for (int i = 0; i < 100; i++) {
        TEST_ASSERT_EQUAL(STARK_OK, firmware(&ic));
    }
    pn532_stats_t zero = {0};
    TEST_ASSERT_EQUAL_MEMORY(&zero, &s_d.stats, sizeof zero);
    TEST_ASSERT_EQUAL(100, fake_pn532_stats()->frames);
}

static void test_command_rejects(void)
{
    uint8_t big[260] = {0};
    uint8_t resp[8];
    size_t got;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      pn532_command(&s_d, 0x40, big, 253, resp, sizeof resp, &got, 100));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      pn532_command(&s_d, 0x02, NULL, 0, resp, sizeof resp, NULL, 100));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      pn532_command(&s_d, 0x02, NULL, 0, big, 253, &got, 100));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      pn532_command(&s_d, 0x02, NULL, 0, NULL, 4, &got, 100));
    /* a response longer than cap arrives cut short: corrupt, not an overflow */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, pn532_command(&s_d, 0x02, NULL, 0, resp, 2, &got, 100));
    /* a command without response data */
    TEST_ASSERT_EQUAL(
        STARK_OK, pn532_command(&s_d, 0x14, (const uint8_t[]){1, 0x14, 1}, 3, NULL, 0, &got, 100));
    TEST_ASSERT_EQUAL(0, got);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_ack_and_response);
    RUN_TEST(test_nack_retried);
    RUN_TEST(test_bad_ack_retried);
    RUN_TEST(test_missing_ack);
    RUN_TEST(test_checksum_error_resent);
    RUN_TEST(test_persistent_checksum_error);
    RUN_TEST(test_truncated_response);
    RUN_TEST(test_never_ready_times_out_and_aborts);
    RUN_TEST(test_bus_errors);
    RUN_TEST(test_no_device);
    RUN_TEST(test_absent_during_poll);
    RUN_TEST(test_unexpected_responses);
    RUN_TEST(test_missed_frame_on_full_read);
    RUN_TEST(test_detect);
    RUN_TEST(test_detect_step_failures);
    RUN_TEST(test_repeated_queries_no_drift);
    RUN_TEST(test_command_rejects);
    return UNITY_END();
}
