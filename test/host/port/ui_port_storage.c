/*
 * ui_port_storage.c — host UI test port: settable stark_storage backend
 * (ui_port_storage.h). stark_storage_state_str() is the production one
 * (storage_core.c), linked alongside.
 */
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

/* File helpers: the UI port never runs a worker (ui_port_worker.c), so these
 * are only linked, never reached with a mounted card. */
stark_err_t stark_storage_open(stark_dir_t dir, const char *name, stark_file_mode_t mode,
                               stark_file_t **out)
{
    (void)dir;
    (void)name;
    (void)mode;
    (void)out;
    return STARK_ERR_STATE;
}

stark_err_t stark_storage_read(stark_file_t *f, void *buf, size_t len, size_t *got)
{
    (void)f;
    (void)buf;
    (void)len;
    (void)got;
    return STARK_ERR_STATE;
}

stark_err_t stark_storage_write(stark_file_t *f, const void *buf, size_t len)
{
    (void)f;
    (void)buf;
    (void)len;
    return STARK_ERR_STATE;
}

stark_err_t stark_storage_close(stark_file_t *f)
{
    (void)f;
    return STARK_ERR_STATE;
}

void stark_storage_discard(stark_file_t *f)
{
    (void)f;
}

stark_err_t stark_storage_list(stark_dir_t dir, stark_storage_list_fn fn, void *ctx)
{
    (void)dir;
    (void)fn;
    (void)ctx;
    return s_state == STARK_STORAGE_MOUNTED ? STARK_OK : STARK_ERR_STATE;
}
