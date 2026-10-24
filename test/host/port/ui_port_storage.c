/*
 * ui_port_storage.c — host UI test port: settable stark_storage backend
 * with an in-memory captures/ directory (ui_port_storage.h). stark_storage_state_str() is the
 * production one (storage_core.c), linked alongside.
 */
#include <stdio.h>
#include <string.h>
#include "stark_event.h"
#include "ui_port_storage.h"

static stark_storage_state_t s_state = STARK_STORAGE_NOT_FITTED;
static stark_medium_space_t s_space;
static int s_ejects, s_mounts;

void ui_port_storage_set(stark_storage_state_t state, uint64_t total, uint64_t free_bytes)
{
    s_state = state;
    s_space.total_bytes = total;
    s_space.free_bytes = free_bytes;
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_STORAGE;
    e.system.arg = (int32_t)state;
    (void)stark_event_publish(&e);
}

int ui_port_storage_ejects(void)
{
    return s_ejects;
}

int ui_port_storage_mounts(void)
{
    return s_mounts;
}

stark_err_t stark_storage_init(void)
{
    return STARK_OK;
}

stark_storage_state_t stark_storage_state(void)
{
    return s_state;
}

stark_err_t stark_storage_space(stark_medium_space_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_state != STARK_STORAGE_MOUNTED) {
        return STARK_ERR_STATE;
    }
    *out = s_space;
    return STARK_OK;
}

stark_err_t stark_storage_request_eject(void)
{
    s_ejects++;
    return STARK_OK;
}

stark_err_t stark_storage_request_mount(void)
{
    s_mounts++;
    return STARK_OK;
}

/* ---- captures/ in memory ---- */
#define FILES     20
#define NAME_MAX_ 24
#define TEXT_MAX  2048

static struct {
    bool used;
    char name[NAME_MAX_];
    char text[TEXT_MAX];
    size_t len;
} s_files[FILES];

struct stark_file {
    int idx;
    size_t pos;
    bool write;
};
static struct stark_file s_handle;
static bool s_handle_open;

static int find(const char *name)
{
    for (int i = 0; i < FILES; i++) {
        if (s_files[i].used && strcmp(s_files[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int put(const char *name, const char *text, size_t len)
{
    int i = find(name);
    for (int j = 0; i < 0 && j < FILES; j++) {
        if (!s_files[j].used) {
            i = j;
        }
    }
    if (i < 0 || strlen(name) >= NAME_MAX_ || len >= TEXT_MAX) {
        return -1;
    }
    s_files[i].used = true;
    snprintf(s_files[i].name, sizeof s_files[i].name, "%s", name);
    memcpy(s_files[i].text, text, len);
    s_files[i].text[len] = '\0';
    s_files[i].len = len;
    return i;
}

void ui_port_storage_add_file(const char *name, const char *text)
{
    (void)put(name, text, strlen(text));
}

void ui_port_storage_clear_files(void)
{
    memset(s_files, 0, sizeof s_files);
}

const char *ui_port_storage_file(const char *name)
{
    int i = find(name);
    return i < 0 ? NULL : s_files[i].text;
}

stark_err_t stark_storage_open(stark_dir_t dir, const char *name, stark_file_mode_t mode,
                               stark_file_t **out)
{
    if (name == NULL || out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_state != STARK_STORAGE_MOUNTED || s_handle_open) {
        return STARK_ERR_STATE;
    }
    bool write = mode != STARK_FILE_READ;
    int i = dir == STARK_DIR_CAPTURES ? (write ? put(name, "", 0) : find(name)) : -1;
    if (i < 0) {
        return write ? STARK_ERR_NO_MEM : STARK_ERR_NOT_FOUND;
    }
    s_handle = (struct stark_file){.idx = i, .pos = 0, .write = write};
    s_handle_open = true;
    *out = &s_handle;
    return STARK_OK;
}

stark_err_t stark_storage_read(stark_file_t *f, void *buf, size_t len, size_t *got)
{
    if (f != &s_handle || !s_handle_open || f->write || buf == NULL || got == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    size_t left = s_files[f->idx].len - f->pos;
    size_t n = len < left ? len : left;
    memcpy(buf, s_files[f->idx].text + f->pos, n);
    f->pos += n;
    *got = n;
    return STARK_OK;
}

stark_err_t stark_storage_write(stark_file_t *f, const void *buf, size_t len)
{
    if (f != &s_handle || !s_handle_open || !f->write || buf == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_files[f->idx].len + len >= TEXT_MAX) {
        return STARK_ERR_NO_MEM;
    }
    memcpy(s_files[f->idx].text + s_files[f->idx].len, buf, len);
    s_files[f->idx].len += len;
    s_files[f->idx].text[s_files[f->idx].len] = '\0';
    return STARK_OK;
}

stark_err_t stark_storage_close(stark_file_t *f)
{
    if (f != &s_handle || !s_handle_open) {
        return STARK_ERR_INVALID_ARG;
    }
    s_handle_open = false;
    return STARK_OK;
}

void stark_storage_discard(stark_file_t *f)
{
    if (f == &s_handle && s_handle_open) {
        s_files[f->idx].used = false;
        s_handle_open = false;
    }
}

stark_err_t stark_storage_list(stark_dir_t dir, stark_storage_list_fn fn, void *ctx)
{
    if (fn == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    if (s_state != STARK_STORAGE_MOUNTED) {
        return STARK_ERR_STATE;
    }
    for (int i = 0; i < FILES && dir == STARK_DIR_CAPTURES; i++) {
        if (s_files[i].used && !fn(s_files[i].name, (uint32_t)s_files[i].len, ctx)) {
            break;
        }
    }
    return STARK_OK;
}
