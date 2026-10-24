/*
 * test_storage_file.c — the file helpers (TASKS.md STARK-0203) on the real
 * POSIX I/O ops in a temp directory, and through fault_io: round trips,
 * atomic replace, discard, append, list, remove, pool exhaustion, stale
 * handles after a removal, the sweep of orphaned temps, and an injected
 * failure at every step of a 100 kB atomic write — each returns an error,
 * leaks no handle and reports an I/O error at most once.
 */
/* POSIX.1-2008 (getpid, mkdir): the host build is strict C11. */
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
#include "unity.h"

static char s_root[64];
static bool s_mounted;
static uint32_t s_gen;
static int s_lock_depth, s_io_errors;

static void lock(void *ctx)
{
    (void)ctx;
    TEST_ASSERT_EQUAL_MESSAGE(0, s_lock_depth, "lock taken twice");
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
    TEST_ASSERT_EQUAL(1, s_lock_depth);
    *gen = s_gen;
    return s_mounted;
}
static void io_error(void *ctx)
{
    (void)ctx;
    s_io_errors++;
}

static void env(const storage_io_t *io)
{
    const storage_env_t e = {io, s_root, lock, unlock, mounted, io_error, NULL};
    storage_files_init(&e);
}

static char *path_of(const char *dir, const char *name)
{
    static char p[160];
    snprintf(p, sizeof p, "%s/stark/%s/%s", s_root, dir, name);
    return p;
}

static bool exists(const char *dir, const char *name)
{
    struct stat st;
    return stat(path_of(dir, name), &st) == 0;
}

static void prepare(void)
{
    uint32_t swept = 99;
    lock(NULL);
    TEST_ASSERT_EQUAL(STARK_OK, storage_files_prepare(&swept));
    unlock(NULL);
    TEST_ASSERT_EQUAL_UINT32(0, swept);
}

void setUp(void)
{
    /* A fresh directory per test (mkdtemp is hidden by strict POSIX on macOS). */
    static int s_seq;
    snprintf(s_root, sizeof s_root, "/tmp/stark_files_%ld_%d", (long)getpid(), ++s_seq);
    TEST_ASSERT_EQUAL(0, mkdir(s_root, 0777));
    s_mounted = true;
    s_gen = 1;
    s_io_errors = 0;
    fault_io_reset();
    env(&fault_io);
    prepare();
}

void tearDown(void)
{
    char cmd[96];
    snprintf(cmd, sizeof cmd, "rm -rf %s", s_root);
    TEST_ASSERT_EQUAL(0, system(cmd));
}

static stark_err_t write_file(stark_dir_t dir, const char *name, const char *text)
{
    stark_file_t *f = NULL;
    STARK_CHECK_RET(stark_storage_open(dir, name, STARK_FILE_WRITE_ATOMIC, &f));
    stark_err_t err = stark_storage_write(f, text, strlen(text));
    stark_err_t cerr = stark_storage_close(f);
    return err != STARK_OK ? err : cerr;
}

static void read_file(const char *name, char *out, size_t n)
{
    stark_file_t *f = NULL;
    size_t got = 0;
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_open(STARK_DIR_CAPTURES, name, STARK_FILE_READ, &f));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_read(f, out, n - 1, &got));
    out[got] = '\0';
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_close(f));
}

static void test_prepare_creates_dirs(void)
{
    struct stat st;
    TEST_ASSERT_EQUAL(0, stat(path_of("captures", ""), &st));
    TEST_ASSERT_EQUAL(0, stat(path_of("logs", ""), &st));
    TEST_ASSERT_EQUAL(0, stat(path_of("apps", ""), &st));
    TEST_ASSERT_EQUAL(0, stat(path_of("config", ""), &st));
    prepare(); /* again: EEXIST is fine */
}

static void test_round_trip_and_atomic_replace(void)
{
    char buf[32];
    TEST_ASSERT_EQUAL(STARK_OK, write_file(STARK_DIR_CAPTURES, "a.txt", "old"));
    stark_file_t *f = NULL;
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_storage_open(STARK_DIR_CAPTURES, "a.txt", STARK_FILE_WRITE_ATOMIC, &f));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_write(f, "new!", 4));
    read_file("a.txt", buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("old", buf); /* untouched until close */
    TEST_ASSERT_TRUE(exists("captures", "a.txt.tmp"));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_close(f));
    read_file("a.txt", buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("new!", buf);
    TEST_ASSERT_FALSE(exists("captures", "a.txt.tmp"));
    TEST_ASSERT_EQUAL_UINT32(0, storage_files_open_count());
}

static void test_discard_and_append(void)
{
    char buf[32];
    TEST_ASSERT_EQUAL(STARK_OK, write_file(STARK_DIR_CAPTURES, "a.txt", "keep"));
    stark_file_t *f = NULL;
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_storage_open(STARK_DIR_CAPTURES, "a.txt", STARK_FILE_WRITE_ATOMIC, &f));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_write(f, "lost", 4));
    stark_storage_discard(f);
    read_file("a.txt", buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("keep", buf);
    TEST_ASSERT_FALSE(exists("captures", "a.txt.tmp"));

    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_storage_open(STARK_DIR_CAPTURES, "a.txt", STARK_FILE_APPEND, &f));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_write(f, "+1", 2));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_close(f));
    read_file("a.txt", buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("keep+1", buf);
    /* append creates a missing file; read-mode discard just closes */
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_storage_open(STARK_DIR_LOGS, "new.log", STARK_FILE_APPEND, &f));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_close(f));
    TEST_ASSERT_TRUE(exists("logs", "new.log"));
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_storage_open(STARK_DIR_CAPTURES, "a.txt", STARK_FILE_READ, &f));
    stark_storage_discard(f);
    TEST_ASSERT_EQUAL_UINT32(0, storage_files_open_count());
}

static bool collect(const char *name, uint32_t size, void *ctx)
{
    char *acc = ctx;
    char item[48];
    snprintf(item, sizeof item, "%s:%u ", name, (unsigned)size);
    strcat(acc, item);
    return strcmp(name, "stop") != 0;
}

static void test_list_and_remove(void)
{
    char acc[128] = "";
    TEST_ASSERT_EQUAL(STARK_OK, write_file(STARK_DIR_CONFIG, "x.cfg", "12345"));
    stark_file_t *f = NULL;
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_storage_open(STARK_DIR_CONFIG, "y.cfg", STARK_FILE_WRITE_ATOMIC, &f));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_list(STARK_DIR_CONFIG, collect, acc));
    TEST_ASSERT_EQUAL_STRING("x.cfg:5 ", acc); /* the temp file is hidden */
    stark_storage_discard(f);
    TEST_ASSERT_EQUAL(STARK_OK, write_file(STARK_DIR_CONFIG, "stop", "s"));
    TEST_ASSERT_EQUAL(STARK_OK, write_file(STARK_DIR_CONFIG, "z", "z"));
    acc[0] = '\0';
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_list(STARK_DIR_CONFIG, collect, acc));
    TEST_ASSERT_NOT_NULL(strstr(acc, "stop:1"));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_remove(STARK_DIR_CONFIG, "x.cfg"));
    TEST_ASSERT_FALSE(exists("config", "x.cfg"));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, stark_storage_remove(STARK_DIR_CONFIG, "x.cfg"));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_storage_remove(STARK_DIR_CONFIG, "../x"));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_storage_list(STARK_DIR_CONFIG, NULL, NULL));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_storage_list((stark_dir_t)7, collect, acc));
}

static void test_argument_and_state_errors(void)
{
    stark_file_t *f = NULL;
    char buf[4];
    size_t got;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_storage_open(STARK_DIR_CAPTURES, "a", STARK_FILE_READ, NULL));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_storage_open(STARK_DIR_CAPTURES, "a", (stark_file_mode_t)9, &f));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_storage_open(STARK_DIR_CAPTURES, ".a", STARK_FILE_READ, &f));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND,
                      stark_storage_open(STARK_DIR_CAPTURES, "none", STARK_FILE_READ, &f));
    TEST_ASSERT_NULL(f);
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_storage_read(NULL, buf, 1, &got));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_storage_read(f, NULL, 1, &got));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_storage_write(NULL, NULL, 1));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_storage_close(NULL));
    stark_storage_discard(NULL); /* harmless */
    /* wrong direction */
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_open(STARK_DIR_CAPTURES, "w", STARK_FILE_APPEND, &f));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, stark_storage_read(f, buf, 1, &got));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_close(f));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_open(STARK_DIR_CAPTURES, "w", STARK_FILE_READ, &f));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, stark_storage_write(f, "x", 1));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_close(f));
    /* not mounted */
    s_mounted = false;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE,
                      stark_storage_open(STARK_DIR_CAPTURES, "w", STARK_FILE_READ, &f));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, stark_storage_remove(STARK_DIR_CAPTURES, "w"));
    char acc[8] = "";
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, stark_storage_list(STARK_DIR_CAPTURES, collect, acc));
    TEST_ASSERT_EQUAL(0, s_io_errors);
}

static void test_pool_exhaustion(void)
{
    stark_file_t *f[STORAGE_FILE_SLOTS + 1];
    char name[8];
    for (int i = 0; i < STORAGE_FILE_SLOTS; i++) {
        snprintf(name, sizeof name, "f%d", i);
        TEST_ASSERT_EQUAL(STARK_OK,
                          stark_storage_open(STARK_DIR_APPS, name, STARK_FILE_APPEND, &f[i]));
    }
    TEST_ASSERT_EQUAL_UINT32(STORAGE_FILE_SLOTS, storage_files_open_count());
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, stark_storage_open(STARK_DIR_APPS, "more", STARK_FILE_APPEND,
                                                         &f[STORAGE_FILE_SLOTS]));
    for (int i = 0; i < STORAGE_FILE_SLOTS; i++) {
        TEST_ASSERT_EQUAL(STARK_OK, stark_storage_close(f[i]));
    }
    TEST_ASSERT_EQUAL_UINT32(0, storage_files_open_count());
}

static void test_stale_handles_after_removal(void)
{
    stark_file_t *w = NULL, *r = NULL;
    char buf[4];
    size_t got;
    TEST_ASSERT_EQUAL(STARK_OK, write_file(STARK_DIR_CAPTURES, "r", "abc"));
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_storage_open(STARK_DIR_CAPTURES, "w", STARK_FILE_WRITE_ATOMIC, &w));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_open(STARK_DIR_CAPTURES, "r", STARK_FILE_READ, &r));
    /* the storage task, on removal: invalidate under the lock, new generation */
    lock(NULL);
    storage_files_invalidate();
    unlock(NULL);
    s_gen++;
    int closes = fault_io_calls(FAULT_CLOSE);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, stark_storage_write(w, "x", 1));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, stark_storage_read(r, buf, 1, &got));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, stark_storage_close(w));
    stark_storage_discard(r);
    TEST_ASSERT_EQUAL(closes, fault_io_calls(FAULT_CLOSE)); /* never touched again */
    TEST_ASSERT_EQUAL_UINT32(0, storage_files_open_count());
    TEST_ASSERT_EQUAL(0, s_io_errors); /* stale is not a new I/O error */
    /* a handle of an older generation, even if never invalidated */
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_open(STARK_DIR_CAPTURES, "r", STARK_FILE_READ, &r));
    s_gen++;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, stark_storage_read(r, buf, 1, &got));
    s_gen--;
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_close(r));
}

static void test_sweep_orphaned_temps(void)
{
    FILE *fp = fopen(path_of("captures", "cut.bin.tmp"), "w");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);
    TEST_ASSERT_EQUAL(STARK_OK, write_file(STARK_DIR_CAPTURES, "ok.bin", "x"));
    uint32_t swept = 0;
    lock(NULL);
    TEST_ASSERT_EQUAL(STARK_OK, storage_files_prepare(&swept));
    unlock(NULL);
    TEST_ASSERT_EQUAL_UINT32(1, swept);
    TEST_ASSERT_FALSE(exists("captures", "cut.bin.tmp"));
    TEST_ASSERT_TRUE(exists("captures", "ok.bin"));
}

static void test_prepare_failures(void)
{
    lock(NULL);
    fault_io_fail_nth(FAULT_MKDIR, 1, EIO);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, storage_files_prepare(NULL));
    fault_io_fail_nth(FAULT_MKDIR, 3, EIO); /* a directory below stark/ */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, storage_files_prepare(NULL));
    fault_io_fail_nth(FAULT_LIST, 1, EIO);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, storage_files_prepare(NULL));
    unlock(NULL);
    TEST_ASSERT_EQUAL(3, s_io_errors);
}

/* 100 kB in 4 kB chunks, atomically. */
static stark_err_t big_write(void)
{
    static uint8_t chunk[4096];
    memset(chunk, 0xA5, sizeof chunk);
    stark_file_t *f = NULL;
    STARK_CHECK_RET(stark_storage_open(STARK_DIR_CAPTURES, "big", STARK_FILE_WRITE_ATOMIC, &f));
    for (int i = 0; i < 25; i++) {
        stark_err_t err = stark_storage_write(f, chunk, sizeof chunk);
        if (err != STARK_OK) {
            stark_storage_discard(f);
            return err;
        }
    }
    return stark_storage_close(f);
}

static void test_eio_at_every_step(void)
{
    static const struct {
        fault_op_t op;
        int nth;
    } steps[] = {
        {FAULT_OPEN, 1},  {FAULT_WRITE, 1}, {FAULT_WRITE, 13}, {FAULT_WRITE, 25},
        {FAULT_FSYNC, 1}, {FAULT_CLOSE, 1}, {FAULT_UNLINK, 1}, {FAULT_RENAME, 1},
    };
    TEST_ASSERT_EQUAL(STARK_OK, big_write()); /* so close has an old file to unlink */
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        s_io_errors = 0;
        fault_io_reset();
        fault_io_fail_nth(steps[i].op, steps[i].nth, EIO);
        TEST_ASSERT_EQUAL_MESSAGE(STARK_ERR_IO, big_write(), "step");
        TEST_ASSERT_EQUAL_MESSAGE(1, s_io_errors, "reported once");
        TEST_ASSERT_EQUAL_UINT32(0, storage_files_open_count());
        TEST_ASSERT_EQUAL(0, s_lock_depth);
    }
    TEST_ASSERT_EQUAL(STARK_OK, big_write()); /* and the card still works */
}

static void test_short_writes_complete(void)
{
    fault_io_short_writes(1000);
    TEST_ASSERT_EQUAL(STARK_OK, big_write());
    struct stat st;
    TEST_ASSERT_EQUAL(0, stat(path_of("captures", "big"), &st));
    TEST_ASSERT_EQUAL(25 * 4096, (int)st.st_size);
}

static void test_no_space(void)
{
    /* ENOSPC from write: NO_SPACE, partial temp dropped, not an I/O error */
    fault_io_fail_nth(FAULT_WRITE, 5, ENOSPC);
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, big_write());
    TEST_ASSERT_FALSE(exists("captures", "big.tmp"));
    /* a 0-byte write is FAT's "full" */
    fault_io_reset();
    fault_io_zero_writes(true);
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, big_write());
    /* full at commit time: fsync ENOSPC drops the temp file too */
    fault_io_reset();
    fault_io_fail_nth(FAULT_FSYNC, 1, ENOSPC);
    TEST_ASSERT_EQUAL(STARK_ERR_NO_SPACE, big_write());
    TEST_ASSERT_FALSE(exists("captures", "big.tmp"));
    TEST_ASSERT_EQUAL(0, s_io_errors);
}

static void test_read_and_list_failures(void)
{
    stark_file_t *f = NULL;
    char buf[4], acc[8] = "";
    size_t got;
    TEST_ASSERT_EQUAL(STARK_OK, write_file(STARK_DIR_CAPTURES, "r", "abc"));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_open(STARK_DIR_CAPTURES, "r", STARK_FILE_READ, &f));
    fault_io_fail_nth(FAULT_READ, 1, EIO);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, stark_storage_read(f, buf, 1, &got));
    TEST_ASSERT_EQUAL(STARK_OK, stark_storage_close(f));
    fault_io_fail_nth(FAULT_LIST, 1, EIO);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, stark_storage_list(STARK_DIR_CAPTURES, collect, acc));
    fault_io_fail_nth(FAULT_UNLINK, 1, EIO);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, stark_storage_remove(STARK_DIR_CAPTURES, "r"));
    TEST_ASSERT_EQUAL(3, s_io_errors);
}

static void test_posix_ops_directly(void)
{
    /* The production table on its own, including a directory it cannot open. */
    env(&storage_io_posix);
    TEST_ASSERT_EQUAL(STARK_OK, write_file(STARK_DIR_CAPTURES, "p", "q"));
    TEST_ASSERT_EQUAL(-1, storage_io_posix.list("/nonexistent-stark-dir", collect, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_prepare_creates_dirs);
    RUN_TEST(test_round_trip_and_atomic_replace);
    RUN_TEST(test_discard_and_append);
    RUN_TEST(test_list_and_remove);
    RUN_TEST(test_argument_and_state_errors);
    RUN_TEST(test_pool_exhaustion);
    RUN_TEST(test_stale_handles_after_removal);
    RUN_TEST(test_sweep_orphaned_temps);
    RUN_TEST(test_prepare_failures);
    RUN_TEST(test_eio_at_every_step);
    RUN_TEST(test_short_writes_complete);
    RUN_TEST(test_no_space);
    RUN_TEST(test_read_and_list_failures);
    RUN_TEST(test_posix_ops_directly);
    return UNITY_END();
}
