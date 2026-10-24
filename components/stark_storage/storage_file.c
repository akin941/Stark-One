/*
 * storage_file.c — the file helpers (storage_file.h, stark_storage.h).
 *
 * Each public call takes the lock for that call only, never across calls,
 * so probes, eject and other writers interleave with long streams. A handle
 * belongs to one mount generation: once the volume goes away its descriptor
 * is closed (storage_files_invalidate) and every later call on it is
 * STARK_ERR_IO without touching the file system.
 */
#include "storage_file.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include "stark_storage.h"
#include "storage_path.h"

struct stark_file {
    bool used;
    bool stale;
    int fd;
    uint32_t generation;
    stark_file_mode_t mode;
    char path[STORAGE_PATH_MAX]; /* the final name */
    char tmp[STORAGE_PATH_MAX];  /* WRITE_ATOMIC: where the bytes go until close */
};

static storage_env_t s_env;
static struct stark_file s_files[STORAGE_FILE_SLOTS];

void storage_files_init(const storage_env_t *env)
{
    s_env = *env;
    memset(s_files, 0, sizeof s_files);
}

/* ---- helpers (caller holds the lock) --------------------------------- */

/* Maps a failed call's errno; an I/O error is reported to the service. */
static stark_err_t fail(void)
{
    stark_err_t err = storage_map_errno(errno);
    if (err == STARK_ERR_IO && s_env.io_error != NULL) {
        s_env.io_error(s_env.ctx);
    }
    return err;
}

static bool mounted(uint32_t *gen)
{
    return s_env.mounted(s_env.ctx, gen);
}

/* OK when f is an open handle of the current mount. */
static stark_err_t usable(const stark_file_t *f)
{
    if (f == NULL || f < s_files || f >= s_files + STORAGE_FILE_SLOTS || !f->used) {
        return STARK_ERR_INVALID_ARG;
    }
    uint32_t gen = 0;
    if (f->stale || !mounted(&gen) || gen != f->generation) {
        return STARK_ERR_IO; /* the card went away under this handle */
    }
    return STARK_OK;
}

static void release(stark_file_t *f)
{
    memset(f, 0, sizeof *f);
}

typedef struct {
    const char *dir;
    uint32_t count;
} sweep_ctx_t;

/* Deletes "<dir>/<name>" when name is a temp file: an atomic write that a
 * removal or power loss interrupted. */
static bool sweep_one(const char *name, uint32_t size, void *ctx)
{
    (void)size;
    sweep_ctx_t *sc = ctx;
    size_t d = strlen(sc->dir);
    size_t n = strlen(name);
    char path[STORAGE_PATH_MAX + STORAGE_NAME_MAX];
    if (n < 4 || strcmp(name + n - 4, ".tmp") != 0 || d + 1 + n >= sizeof path) {
        return true;
    }
    memcpy(path, sc->dir, d);
    path[d] = '/';
    memcpy(path + d + 1, name, n + 1);
    if (s_env.io->unlink(path) == 0) {
        sc->count++;
    }
    return true;
}

static stark_err_t make_dir(const char *path)
{
    return s_env.io->mkdir(path) == 0 || errno == EEXIST ? STARK_OK : fail();
}

stark_err_t storage_files_prepare(uint32_t *swept)
{
    char path[STORAGE_PATH_MAX];
    sweep_ctx_t sc = {path, 0};
    /* "<root>/stark" first, then each directory below it. */
    STARK_CHECK_RET(storage_dir_path(s_env.root, STARK_DIR_CAPTURES, path, sizeof path));
    *strrchr(path, '/') = '\0';
    STARK_CHECK_RET(make_dir(path));
    for (int d = STARK_DIR_CAPTURES; d <= STARK_DIR_CONFIG; d++) {
        STARK_CHECK_RET(storage_dir_path(s_env.root, (stark_dir_t)d, path, sizeof path));
        STARK_CHECK_RET(make_dir(path));
        if (s_env.io->list(path, sweep_one, &sc) != 0) {
            return fail();
        }
    }
    if (swept != NULL) {
        *swept = sc.count;
    }
    return STARK_OK;
}

void storage_files_invalidate(void)
{
    for (int i = 0; i < STORAGE_FILE_SLOTS; i++) {
        if (s_files[i].used && !s_files[i].stale) {
            (void)s_env.io->close(s_files[i].fd); /* fails on a pulled card: still released */
            s_files[i].stale = true;
        }
    }
}

uint32_t storage_files_open_count(void)
{
    uint32_t n = 0;
    for (int i = 0; i < STORAGE_FILE_SLOTS; i++) {
        n += s_files[i].used ? 1u : 0u;
    }
    return n;
}

/* ---- public API ------------------------------------------------------- */

stark_err_t stark_storage_open(stark_dir_t dir, const char *name, stark_file_mode_t mode,
                               stark_file_t **out)
{
    if (out == NULL || (unsigned)mode > STARK_FILE_APPEND) {
        return STARK_ERR_INVALID_ARG;
    }
    *out = NULL;
    stark_file_t f = {.used = true, .mode = mode};
    STARK_CHECK_RET(storage_path_build(s_env.root, dir, name, false, f.path, sizeof f.path));
    STARK_CHECK_RET(storage_path_build(s_env.root, dir, name, true, f.tmp, sizeof f.tmp));

    s_env.lock(s_env.ctx);
    stark_err_t err = STARK_OK;
    stark_file_t *slot = NULL;
    for (int i = 0; i < STORAGE_FILE_SLOTS && slot == NULL; i++) {
        slot = s_files[i].used ? NULL : &s_files[i];
    }
    if (!mounted(&f.generation)) {
        err = STARK_ERR_STATE;
    } else if (slot == NULL) {
        err = STARK_ERR_BUSY;
    } else {
        static const int flags[] = {
            [STARK_FILE_READ] = O_RDONLY,
            [STARK_FILE_WRITE_ATOMIC] = O_WRONLY | O_CREAT | O_TRUNC,
            [STARK_FILE_APPEND] = O_WRONLY | O_CREAT | O_APPEND,
        };
        f.fd = s_env.io->open(mode == STARK_FILE_WRITE_ATOMIC ? f.tmp : f.path, flags[mode]);
        if (f.fd < 0) {
            err = fail();
        } else {
            *slot = f;
            *out = slot;
        }
    }
    s_env.unlock(s_env.ctx);
    return err;
}

stark_err_t stark_storage_read(stark_file_t *f, void *buf, size_t len, size_t *got)
{
    if (buf == NULL || got == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    *got = 0;
    s_env.lock(s_env.ctx);
    stark_err_t err = usable(f);
    if (err == STARK_OK && f->mode != STARK_FILE_READ) {
        err = STARK_ERR_STATE;
    }
    if (err == STARK_OK) {
        ssize_t n = s_env.io->read(f->fd, buf, len);
        if (n < 0) {
            err = fail();
        } else {
            *got = (size_t)n;
        }
    }
    s_env.unlock(s_env.ctx);
    return err;
}

stark_err_t stark_storage_write(stark_file_t *f, const void *buf, size_t len)
{
    if (buf == NULL && len > 0) {
        return STARK_ERR_INVALID_ARG;
    }
    s_env.lock(s_env.ctx);
    stark_err_t err = usable(f);
    if (err == STARK_OK && f->mode == STARK_FILE_READ) {
        err = STARK_ERR_STATE;
    }
    const uint8_t *p = buf;
    while (err == STARK_OK && len > 0) {
        ssize_t n = s_env.io->write(f->fd, p, len);
        if (n < 0) {
            err = fail();
        } else if (n == 0) {
            err = STARK_ERR_NO_SPACE; /* FAT reports a full volume as a 0-byte write */
        } else {
            p += n;
            len -= (size_t)n;
        }
    }
    s_env.unlock(s_env.ctx);
    return err;
}

stark_err_t stark_storage_close(stark_file_t *f)
{
    s_env.lock(s_env.ctx);
    stark_err_t err = usable(f);
    if (err == STARK_ERR_INVALID_ARG) {
        s_env.unlock(s_env.ctx);
        return err;
    }
    if (err == STARK_OK) {
        bool atomic = f->mode == STARK_FILE_WRITE_ATOMIC;
        if (f->mode != STARK_FILE_READ && s_env.io->fsync(f->fd) != 0) {
            err = fail();
        }
        if (s_env.io->close(f->fd) != 0 && err == STARK_OK) {
            err = fail();
        }
        /* FAT cannot rename over a file: the old one goes first (ADR-0007 —
         * the window between the two is not power-loss safe; a temp file
         * left behind is swept at the next mount). */
        if (atomic && err == STARK_OK && s_env.io->unlink(f->path) != 0 && errno != ENOENT) {
            err = fail();
        }
        if (atomic && err == STARK_OK && s_env.io->rename(f->tmp, f->path) != 0) {
            err = fail();
        }
        if (atomic && err != STARK_OK && err != STARK_ERR_IO) {
            (void)s_env.io->unlink(f->tmp); /* e.g. no space: drop the partial copy */
        }
    }
    release(f); /* a stale handle's descriptor was closed at invalidation */
    s_env.unlock(s_env.ctx);
    return err;
}

void stark_storage_discard(stark_file_t *f)
{
    s_env.lock(s_env.ctx);
    stark_err_t err = usable(f);
    if (err == STARK_OK) {
        (void)s_env.io->close(f->fd);
        if (f->mode == STARK_FILE_WRITE_ATOMIC) {
            (void)s_env.io->unlink(f->tmp);
        }
    }
    if (err != STARK_ERR_INVALID_ARG) {
        release(f);
    }
    s_env.unlock(s_env.ctx);
}

stark_err_t stark_storage_remove(stark_dir_t dir, const char *name)
{
    char path[STORAGE_PATH_MAX];
    STARK_CHECK_RET(storage_path_build(s_env.root, dir, name, false, path, sizeof path));
    s_env.lock(s_env.ctx);
    uint32_t gen = 0;
    stark_err_t err = STARK_OK;
    if (!mounted(&gen)) {
        err = STARK_ERR_STATE;
    } else if (s_env.io->unlink(path) != 0) {
        err = fail();
    }
    s_env.unlock(s_env.ctx);
    return err;
}

typedef struct {
    stark_storage_list_fn fn;
    void *ctx;
} list_ctx_t;

/* Hides in-progress temp files from listings. */
static bool list_one(const char *name, uint32_t size, void *ctx)
{
    const list_ctx_t *lc = ctx;
    return storage_name_valid(name) ? lc->fn(name, size, lc->ctx) : true;
}

stark_err_t stark_storage_list(stark_dir_t dir, stark_storage_list_fn fn, void *ctx)
{
    char path[STORAGE_PATH_MAX];
    if (fn == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    STARK_CHECK_RET(storage_dir_path(s_env.root, dir, path, sizeof path));
    s_env.lock(s_env.ctx);
    uint32_t gen = 0;
    stark_err_t err = STARK_OK;
    list_ctx_t lc = {fn, ctx};
    if (!mounted(&gen)) {
        err = STARK_ERR_STATE;
    } else if (s_env.io->list(path, list_one, &lc) != 0) {
        err = fail();
    }
    s_env.unlock(s_env.ctx);
    return err;
}
