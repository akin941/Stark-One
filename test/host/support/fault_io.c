/*
 * fault_io.c — a fault-injecting storage_io_t over storage_io_posix
 * (fault_io.h).
 */
#include "fault_io.h"
#include <errno.h>
#include <string.h>

static int s_calls[FAULT_OPS];
static int s_fail_at[FAULT_OPS];
static int s_fail_err[FAULT_OPS];
static size_t s_short;
static bool s_zero;

void fault_io_reset(void)
{
    memset(s_calls, 0, sizeof s_calls);
    memset(s_fail_at, 0, sizeof s_fail_at);
    s_short = 0;
    s_zero = false;
}

void fault_io_fail_nth(fault_op_t op, int nth, int err)
{
    s_fail_at[op] = nth == 0 ? 0 : s_calls[op] + nth;
    s_fail_err[op] = err;
}

void fault_io_short_writes(size_t max)
{
    s_short = max;
}

void fault_io_zero_writes(bool on)
{
    s_zero = on;
}

int fault_io_calls(fault_op_t op)
{
    return s_calls[op];
}

/* Counts the call; true (errno set) when this one must fail. */
static bool fails(fault_op_t op)
{
    s_calls[op]++;
    if (s_fail_at[op] != 0 && s_calls[op] == s_fail_at[op]) {
        errno = s_fail_err[op];
        return true;
    }
    return false;
}

static int f_open(const char *p, int fl)
{
    return fails(FAULT_OPEN) ? -1 : storage_io_posix.open(p, fl);
}
static ssize_t f_read(int fd, void *b, size_t n)
{
    return fails(FAULT_READ) ? -1 : storage_io_posix.read(fd, b, n);
}
static ssize_t f_write(int fd, const void *b, size_t n)
{
    if (fails(FAULT_WRITE)) {
        return -1;
    }
    if (s_zero) {
        return 0;
    }
    return storage_io_posix.write(fd, b, s_short != 0 && n > s_short ? s_short : n);
}
static int f_fsync(int fd)
{
    return fails(FAULT_FSYNC) ? -1 : storage_io_posix.fsync(fd);
}
static int f_close(int fd)
{
    if (fails(FAULT_CLOSE)) {
        (void)storage_io_posix.close(fd); /* the descriptor is gone either way */
        return -1;
    }
    return storage_io_posix.close(fd);
}
static int f_rename(const char *a, const char *b)
{
    return fails(FAULT_RENAME) ? -1 : storage_io_posix.rename(a, b);
}
static int f_unlink(const char *p)
{
    return fails(FAULT_UNLINK) ? -1 : storage_io_posix.unlink(p);
}
static int f_mkdir(const char *p)
{
    return fails(FAULT_MKDIR) ? -1 : storage_io_posix.mkdir(p);
}
static int f_list(const char *d, storage_list_fn fn, void *ctx)
{
    return fails(FAULT_LIST) ? -1 : storage_io_posix.list(d, fn, ctx);
}

const storage_io_t fault_io = {f_open,   f_read,   f_write, f_fsync, f_close,
                               f_rename, f_unlink, f_mkdir, f_list};
