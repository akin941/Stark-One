/*
 * storage_fake.c — the stark_storage file API in memory for the host tests (storage_fake.h).
 */
#include "storage_fake.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILES     12
#define NAME_MAX_ 33
#define CAP       (1100u * 1024u)

typedef struct {
    bool used;
    stark_dir_t dir;
    char name[NAME_MAX_];
    uint8_t *data;
    size_t len;
} file_t;

struct stark_file {
    int idx;
    size_t pos;
    stark_file_mode_t mode;
    bool open;
};

static file_t s_files[FILES];
static struct stark_file s_handle;
static stark_storage_state_t s_state = STARK_STORAGE_MOUNTED;
static bool s_armed[SFAKE_KINDS];
static int s_after[SFAKE_KINDS];
static unsigned s_closes;
static unsigned s_writes;

void sfake_reset(void)
{
    for (int i = 0; i < FILES; i++) {
        free(s_files[i].data);
    }
    memset(s_files, 0, sizeof s_files);
    memset(&s_handle, 0, sizeof s_handle);
    memset(s_armed, 0, sizeof s_armed);
    memset(s_after, 0, sizeof s_after);
    s_state = STARK_STORAGE_MOUNTED;
    s_closes = 0;
    s_writes = 0;
}

void sfake_set_state(stark_storage_state_t state)
{
    s_state = state;
}

static int find(stark_dir_t dir, const char *name)
{
    for (int i = 0; i < FILES; i++) {
        if (s_files[i].used && s_files[i].dir == dir && strcmp(s_files[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int create(stark_dir_t dir, const char *name)
{
    for (int i = 0; i < FILES; i++) {
        if (!s_files[i].used) {
            s_files[i].used = true;
            s_files[i].dir = dir;
            snprintf(s_files[i].name, sizeof s_files[i].name, "%s", name);
            s_files[i].data = malloc(CAP);
            s_files[i].len = 0;
            return i;
        }
    }
    return -1;
}

void sfake_put(stark_dir_t dir, const char *name, const uint8_t *data, size_t n)
{
    int i = find(dir, name);
    if (i < 0) {
        i = create(dir, name);
    }
    if (i >= 0 && n <= CAP) {
        memcpy(s_files[i].data, data, n);
        s_files[i].len = n;
    }
}

const uint8_t *sfake_get(stark_dir_t dir, const char *name, size_t *n)
{
    int i = find(dir, name);
    if (i < 0) {
        *n = 0;
        return NULL;
    }
    *n = s_files[i].len;
    return s_files[i].data;
}

size_t sfake_files(stark_dir_t dir)
{
    size_t n = 0;
    for (int i = 0; i < FILES; i++) {
        n += s_files[i].used && s_files[i].dir == dir ? 1u : 0u;
    }
    return n;
}

void sfake_fail(sfake_call_t call, int after)
{
    if ((unsigned)call < (unsigned)SFAKE_KINDS) {
        s_armed[call] = after >= 0;
        s_after[call] = after >= 0 ? after : 0;
    }
}

static bool hit(sfake_call_t call)
{
    if (!s_armed[call]) {
        return false;
    }
    if (s_after[call] > 0) {
        s_after[call]--;
        return false;
    }
    return true;
}

unsigned sfake_open_handles(void)
{
    return s_handle.open ? 1u : 0u;
}

unsigned sfake_closes(void)
{
    return s_closes;
}

unsigned sfake_writes(void)
{
    return s_writes;
}

stark_storage_state_t stark_storage_state(void)
{
    return s_state;
}

const char *stark_storage_state_str(stark_storage_state_t state)
{
    switch (state) {
        case STARK_STORAGE_NOT_FITTED:
            return "not fitted";
        case STARK_STORAGE_ABSENT:
            return "no card";
        case STARK_STORAGE_MOUNTED:
            return "mounted";
        case STARK_STORAGE_UNUSABLE:
            return "unusable";
        case STARK_STORAGE_REMOVED:
            return "removed";
        case STARK_STORAGE_EJECTED:
            return "ejected";
        default:
            return "?";
    }
}

stark_err_t stark_storage_list(stark_dir_t dir, stark_storage_list_fn fn, void *ctx)
{
    if (s_state != STARK_STORAGE_MOUNTED) {
        return STARK_ERR_STATE;
    }
    if (hit(SFAKE_LIST)) {
        return STARK_ERR_IO;
    }
    for (int i = 0; i < FILES; i++) {
        if (s_files[i].used && s_files[i].dir == dir &&
            !fn(s_files[i].name, (uint32_t)s_files[i].len, ctx)) {
            break;
        }
    }
    return STARK_OK;
}

stark_err_t stark_storage_open(stark_dir_t dir, const char *name, stark_file_mode_t mode,
                               stark_file_t **out)
{
    if (name == NULL || out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_state != STARK_STORAGE_MOUNTED) {
        return STARK_ERR_STATE;
    }
    if (s_handle.open) {
        return STARK_ERR_BUSY;
    }
    if (hit(SFAKE_OPEN)) {
        return STARK_ERR_IO;
    }
    int i = find(dir, name);
    if (mode == STARK_FILE_READ && i < 0) {
        return STARK_ERR_NOT_FOUND;
    }
    if (i < 0) {
        i = create(dir, name);
    } else if (mode == STARK_FILE_WRITE_ATOMIC) {
        s_files[i].len = 0;
    }
    if (i < 0) {
        return STARK_ERR_NO_SPACE;
    }
    s_handle = (struct stark_file){.idx = i, .mode = mode, .open = true};
    *out = &s_handle;
    return STARK_OK;
}

stark_err_t stark_storage_read(stark_file_t *f, void *buf, size_t len, size_t *got)
{
    if (f != &s_handle || !f->open || f->mode != STARK_FILE_READ || buf == NULL || got == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_state != STARK_STORAGE_MOUNTED || hit(SFAKE_READ)) {
        return STARK_ERR_IO;
    }
    size_t left = s_files[f->idx].len - f->pos;
    size_t n = len < left ? len : left;
    memcpy(buf, s_files[f->idx].data + f->pos, n);
    f->pos += n;
    *got = n;
    return STARK_OK;
}

stark_err_t stark_storage_write(stark_file_t *f, const void *buf, size_t len)
{
    if (f != &s_handle || !f->open || f->mode == STARK_FILE_READ || buf == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_state != STARK_STORAGE_MOUNTED || hit(SFAKE_WRITE)) {
        return STARK_ERR_IO;
    }
    if (s_files[f->idx].len + len > CAP) {
        return STARK_ERR_NO_SPACE;
    }
    memcpy(s_files[f->idx].data + s_files[f->idx].len, buf, len);
    s_files[f->idx].len += len;
    s_writes++;
    return STARK_OK;
}

stark_err_t stark_storage_close(stark_file_t *f)
{
    if (f != &s_handle || !f->open) {
        return STARK_ERR_INVALID_ARG;
    }
    s_handle.open = false; /* freed even when it fails */
    if (s_state != STARK_STORAGE_MOUNTED || hit(SFAKE_CLOSE)) {
        return STARK_ERR_IO;
    }
    s_closes++;
    return STARK_OK;
}

void stark_storage_discard(stark_file_t *f)
{
    if (f == &s_handle) {
        s_handle.open = false;
    }
}
