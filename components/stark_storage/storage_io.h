/*
 * storage_io.h — private to stark_storage: the file-system calls the file
 * helpers make (TASKS.md STARK-0203). The firmware always uses the POSIX
 * implementation (storage_io_posix.c, over the VFS/FATFS mount); host tests
 * use the same file against a temp directory, or wrap it to inject faults.
 * A test seam inside the component — never a second storage path.
 *
 * Every call returns what its POSIX counterpart returns, with errno set on
 * failure.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef bool (*storage_list_fn)(const char *name, uint32_t size, void *ctx);

typedef struct {
    int (*open)(const char *path, int flags); /* mode 0666 when creating */
    ssize_t (*read)(int fd, void *buf, size_t len);
    ssize_t (*write)(int fd, const void *buf, size_t len);
    int (*fsync)(int fd);
    int (*close)(int fd);
    int (*rename)(const char *from, const char *to);
    int (*unlink)(const char *path);
    int (*mkdir)(const char *path); /* mode 0777 */
    /* Calls fn for every regular file in dir until it returns false;
     * 0, or -1 with errno. */
    int (*list)(const char *dir, storage_list_fn fn, void *ctx);
} storage_io_t;

extern const storage_io_t storage_io_posix;
