/*
 * storage_io_posix.c — storage_io.h over POSIX. Compiles unchanged for the
 * target (ESP-IDF VFS → FATFS) and the host (a real directory).
 */
/* POSIX.1-2008 (open, fsync, opendir, mkdtemp): the host build is strict C11. */
#define _POSIX_C_SOURCE 200809L
#include "storage_io.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static int io_open(const char *path, int flags)
{
    return open(path, flags, 0666);
}

static int io_mkdir(const char *path)
{
    return mkdir(path, 0777);
}

static int io_list(const char *dir, storage_list_fn fn, void *ctx)
{
    DIR *d = opendir(dir);
    if (d == NULL) {
        return -1;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char path[160];
        struct stat st;
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path ||
            stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        if (!fn(e->d_name, (uint32_t)st.st_size, ctx)) {
            break;
        }
    }
    int saved = errno;
    (void)closedir(d);
    errno = saved;
    return 0;
}

const storage_io_t storage_io_posix = {
    .open = io_open,
    .read = read,
    .write = write,
    .fsync = fsync,
    .close = close,
    .rename = rename,
    .unlink = unlink,
    .mkdir = io_mkdir,
    .list = io_list,
};
