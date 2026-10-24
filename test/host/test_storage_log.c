/*
 * test_storage_log.c — the log file writer (TASKS.md STARK-0206) on the real
 * POSIX ops in a temp directory and through fault_io, fed by a fake ring:
 * first write, dropped-lines marker, rotation and its bound over 1 MB of
 * lines, a failure at each rotation step, write failures, the disabled
 * writer.
 */
/* POSIX.1-2008 (getpid, mkdir, stat): the host build is strict C11. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fault_io.h"
#include "stark_storage.h"
#include "storage_file.h"
#include "storage_log.h"
#include "unity.h"

static char s_root[64];
static int s_lock_depth;

static void lock(void *ctx)
{
    (void)ctx;
    TEST_ASSERT_EQUAL(0, s_lock_depth);
    s_lock_depth++;
}
static void unlock(void *ctx)
{
    (void)ctx;
    s_lock_depth--;
}
static bool mounted(void *ctx, uint32_t *gen)
{
    (void)ctx;
    *gen = 1;
    return true;
}

/* The fake ring: one pending text and a dropped count, taken whole. */
static const char *s_pending;
static uint32_t s_dropped;

static size_t fake_take(char *buf, size_t max, uint32_t *dropped)
{
    *dropped = s_dropped;
    s_dropped = 0;
    if (s_pending == NULL) {
        return 0;
    }
    size_t n = strlen(s_pending);
    n = n < max ? n : max;
    memcpy(buf, s_pending, n);
    s_pending = NULL;
    return n;
}

static long file_size(const char *name)
{
    char p[160];
    struct stat st;
    snprintf(p, sizeof p, "%s/stark/logs/%s", s_root, name);
    return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

static void read_file(const char *name, char *out, size_t n)
{
    char p[160];
    snprintf(p, sizeof p, "%s/stark/logs/%s", s_root, name);
    FILE *f = fopen(p, "r");
    TEST_ASSERT_NOT_NULL(f);
    size_t got = fread(out, 1, n - 1, f);
    out[got] = '\0';
    fclose(f);
}

void setUp(void)
{
    static int seq;
    snprintf(s_root, sizeof s_root, "/tmp/stark_log_%ld_%d", (long)getpid(), ++seq);
    TEST_ASSERT_EQUAL(0, mkdir(s_root, 0777));
    fault_io_reset();
    const storage_env_t e = {&fault_io, s_root, lock, unlock, mounted, NULL, NULL};
    storage_files_init(&e);
    lock(NULL);
    TEST_ASSERT_EQUAL(STARK_OK, storage_files_prepare(NULL));
    unlock(NULL);
    storage_log_init(fake_take, 100, 3);
    s_pending = NULL;
    s_dropped = 0;
}

void tearDown(void)
{
    char cmd[96];
    snprintf(cmd, sizeof cmd, "rm -rf %s", s_root);
    TEST_ASSERT_EQUAL(0, system(cmd));
}

static void test_first_write_and_marker(void)
{
    bool first = false;
    TEST_ASSERT_EQUAL(STARK_OK, storage_log_flush(&first));
    TEST_ASSERT_FALSE(first); /* nothing to write yet */
    TEST_ASSERT_EQUAL(-1, file_size("stark.log"));
    s_pending = "boot: hello\n";
    TEST_ASSERT_EQUAL(STARK_OK, storage_log_flush(&first));
    TEST_ASSERT_TRUE(first);
    s_pending = "two\n";
    s_dropped = 3;
    TEST_ASSERT_EQUAL(STARK_OK, storage_log_flush(&first));
    TEST_ASSERT_FALSE(first); /* announced once per mount */
    char text[128];
    read_file("stark.log", text, sizeof text);
    TEST_ASSERT_EQUAL_STRING("boot: hello\n--- 3 log lines dropped ---\ntwo\n", text);
    TEST_ASSERT_EQUAL_UINT32(strlen(text), storage_log_written());
    storage_log_on_mount();
    TEST_ASSERT_EQUAL_UINT32(0, storage_log_written());
    s_dropped = 1; /* a marker alone is written too */
    TEST_ASSERT_EQUAL(STARK_OK, storage_log_flush(NULL));
    TEST_ASSERT_TRUE(storage_log_written() > 0);
}

static void test_rotation_and_bound(void)
{
    /* 40-byte lines against a 100-byte limit and 3 files. */
    const char *line = "0123456789012345678901234567890123456\n\n\n";
    for (int i = 0; i < 26000; i++) { /* ~1 MB */
        s_pending = line;
        TEST_ASSERT_EQUAL(STARK_OK, storage_log_flush(NULL));
    }
    TEST_ASSERT_TRUE(file_size("stark.log") <= 100);
    TEST_ASSERT_TRUE(file_size("stark.1.log") > 0 && file_size("stark.1.log") <= 100);
    TEST_ASSERT_TRUE(file_size("stark.2.log") > 0 && file_size("stark.2.log") <= 100);
    TEST_ASSERT_EQUAL(-1, file_size("stark.3.log")); /* never more than 3 files */
}

static void test_existing_file_size_is_read(void)
{
    s_pending = "0123456789012345678901234567890123456789012345678901234567890123456789\n";
    TEST_ASSERT_EQUAL(STARK_OK, storage_log_flush(NULL)); /* 71 bytes */
    storage_log_on_mount();                               /* size read again */
    s_pending = "0123456789012345678901234567890123456\n";
    TEST_ASSERT_EQUAL(STARK_OK, storage_log_flush(NULL)); /* 71 + 38 > 100: rotates */
    TEST_ASSERT_EQUAL(71, file_size("stark.1.log"));
    TEST_ASSERT_EQUAL(38, file_size("stark.log"));
}

static void test_failing_rotation_step_appends_and_retries(void)
{
    s_pending = "0123456789012345678901234567890123456789012345678901234567890123456789\n";
    TEST_ASSERT_EQUAL(STARK_OK, storage_log_flush(NULL));
    fault_io_fail_nth(FAULT_RENAME, 1, EIO); /* stark.log -> stark.1.log fails */
    s_pending = "0123456789012345678901234567890123456\n";
    TEST_ASSERT_EQUAL(STARK_OK, storage_log_flush(NULL));
    TEST_ASSERT_EQUAL(109, file_size("stark.log")); /* appended instead */
    s_pending = "x\n";
    TEST_ASSERT_EQUAL(STARK_OK, storage_log_flush(NULL)); /* retried now */
    TEST_ASSERT_EQUAL(109, file_size("stark.1.log"));
    TEST_ASSERT_EQUAL(2, file_size("stark.log"));
    /* a failing delete of the oldest aborts the same way */
    fault_io_fail_nth(FAULT_UNLINK, 1, EIO);
    s_pending = "0123456789012345678901234567890123456789012345678901234567890123456789"
                "0123456789012345678901234567890\n";
    TEST_ASSERT_EQUAL(STARK_OK, storage_log_flush(NULL));
    TEST_ASSERT_EQUAL(104, file_size("stark.log"));
}

static void test_write_failures(void)
{
    s_pending = "a\n";
    fault_io_fail_nth(FAULT_WRITE, 1, EIO); /* e.g. the card was pulled */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, storage_log_flush(NULL));
    TEST_ASSERT_EQUAL_UINT32(0, storage_log_written());
    TEST_ASSERT_EQUAL_UINT32(0, storage_files_open_count()); /* no handle leaked */
    s_pending = "b\n";
    TEST_ASSERT_EQUAL(STARK_OK, storage_log_flush(NULL)); /* size re-read, recovered */
    fault_io_fail_nth(FAULT_OPEN, 1, EIO);
    s_pending = "c\n";
    TEST_ASSERT_EQUAL(STARK_ERR_IO, storage_log_flush(NULL));
    /* the size cannot be read */
    storage_log_on_mount();
    fault_io_fail_nth(FAULT_LIST, 1, EIO);
    s_pending = "d\n";
    TEST_ASSERT_EQUAL(STARK_ERR_IO, storage_log_flush(NULL));
}

static void test_disabled_writer(void)
{
    storage_log_init(NULL, 100, 0);
    s_pending = "x\n";
    bool first = true;
    TEST_ASSERT_EQUAL(STARK_OK, storage_log_flush(&first));
    TEST_ASSERT_FALSE(first);
    TEST_ASSERT_EQUAL(-1, file_size("stark.log"));
    storage_log_init(fake_take, 100, 99); /* clamped to LOG_ROTATE_MAX_FILES */
    TEST_ASSERT_EQUAL(STARK_OK, storage_log_flush(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_write_and_marker);
    RUN_TEST(test_rotation_and_bound);
    RUN_TEST(test_existing_file_size_is_read);
    RUN_TEST(test_failing_rotation_step_appends_and_retries);
    RUN_TEST(test_write_failures);
    RUN_TEST(test_disabled_writer);
    return UNITY_END();
}
