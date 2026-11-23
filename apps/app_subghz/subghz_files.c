/*
 * subghz_files.c — the Sub-GHz app's card I/O (TASKS.md STARK-0603, docs/subghz-format.md):
 * a capture is written as the next free captures/rf_NNNN.rf, atomically and streamed through
 * a small chunk, and a saved file is read back through the strict incremental parser; both
 * run on the app worker, the card is never touched from the UI task. The one 8 kB symbol
 * buffer is allocated here, the first time it is needed, and freed when the app stops.
 *
 * NOTE: the UI task alone changes `s_busy`; the worker only publishes a notification when it
 * is done, and what it wrote is read after that notification (the event queue orders them).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stark_app.h"
#include "stark_log.h"
#include "stark_storage.h"
#include "subghzapp.h"

#define NOTE_BASE 200 /* STARK_APP_REQ_NOTIFY args: NOTE_BASE + sgfile_note_t */
#define CHUNK     256 /* file bytes streamed at a time */

static subghz_sym_t *s_buf;
static bool s_busy; /* a worker owns the buffer, the save description and the list */
static subghz_raw_t s_save_raw;
static char s_pending_name[16]; /* what the worker saved; s_saved follows on the notification */
static char s_saved[16];
static sgfile_entry_t s_list[SGAPP_LIST_MAX];
static size_t s_count;
static size_t s_pick;
static subghz_raw_t s_loaded;
static char s_loaded_name[16];

subghz_sym_t *sgfile_buffer(void)
{
    if (s_buf == NULL) {
        s_buf = malloc(SUBGHZ_RAW_MAX_SYMBOLS * sizeof s_buf[0]);
    }
    return s_buf;
}

subghz_sym_t *sgfile_symbols(void)
{
    return s_buf;
}

void sgfile_stop(void)
{
    free(s_buf);
    s_buf = NULL;
    s_busy = false;
    s_count = 0;
    s_saved[0] = '\0';
    memset(&s_loaded, 0, sizeof s_loaded);
}

bool sgfile_busy(void)
{
    return s_busy;
}

static void notify(sgfile_note_t what)
{
    stark_event_t e = {.type = STARK_EVT_APP_REQUEST};
    e.app.id = STARK_APP_REQ_NOTIFY;
    e.app.arg = NOTE_BASE + (int32_t)what;
    (void)stark_event_publish(&e);
}

bool sgfile_event(const stark_event_t *e, sgfile_note_t *what)
{
    if (e->type != STARK_EVT_APP_REQUEST || e->app.id != STARK_APP_REQ_NOTIFY ||
        e->app.arg <= NOTE_BASE + (int32_t)SGFILE_NOTE_NONE ||
        e->app.arg > NOTE_BASE + (int32_t)SGFILE_NOTE_OPEN_FAILED) {
        return false;
    }
    *what = (sgfile_note_t)(e->app.arg - NOTE_BASE);
    s_busy = false;
    if (*what == SGFILE_NOTE_SAVED) {
        (void)snprintf(s_saved, sizeof s_saved, "%s", s_pending_name);
    }
    return true;
}

/* ---- save ---------------------------------------------------------------------------- */

static bool scan(const char *name, uint32_t size, void *ctx)
{
    (void)size;
    subghz_name_scan(ctx, name);
    return true;
}

static stark_err_t write_one(char *name, size_t len)
{
    uint32_t highest = 0;
    STARK_CHECK_RET(stark_storage_list(STARK_DIR_CAPTURES, scan, &highest));
    STARK_CHECK_RET(subghz_name_next(highest, name, len));
    stark_file_t *f = NULL;
    STARK_CHECK_RET(stark_storage_open(STARK_DIR_CAPTURES, name, STARK_FILE_WRITE_ATOMIC, &f));
    char chunk[CHUNK];
    size_t cursor = 0;
    for (;;) {
        size_t n = 0;
        stark_err_t err = subghz_raw_format(&s_save_raw, &cursor, chunk, sizeof chunk, &n);
        if (err == STARK_OK && n > 0) {
            err = stark_storage_write(f, chunk, n);
        }
        if (err != STARK_OK) {
            stark_storage_discard(f);
            return err;
        }
        if (n == 0) {
            return stark_storage_close(f);
        }
    }
}

static void save_worker(void *ctx)
{
    (void)ctx;
    char name[16];
    stark_err_t err = write_one(name, sizeof name);
    if (err == STARK_OK) {
        (void)snprintf(s_pending_name, sizeof s_pending_name, "%s", name);
        STARK_LOGI("subghzapp", "saved %s (%u sym) hash 0x%08x", name, (unsigned)s_save_raw.n,
                   (unsigned)subghz_raw_hash(s_save_raw.sym, s_save_raw.n));
        notify(SGFILE_NOTE_SAVED);
    } else {
        STARK_LOGW("subghzapp", "save failed: %s", stark_err_str(err));
        notify(SGFILE_NOTE_SAVE_FAILED);
    }
}

void sgfile_save(const subghz_raw_t *raw)
{
    stark_storage_state_t state = stark_storage_state();
    if (raw == NULL || !subghz_raw_valid(raw)) {
        STARK_LOGI("subghzapp", "save refused: nothing captured");
    } else if (state != STARK_STORAGE_MOUNTED) {
        STARK_LOGI("subghzapp", "save refused: %s", stark_storage_state_str(state));
    } else if (s_busy) {
        STARK_LOGI("subghzapp", "save refused: busy");
    } else {
        s_save_raw = *raw;
        s_busy = true;
        stark_err_t err = stark_app_worker_start(save_worker, NULL);
        if (err != STARK_OK) {
            s_busy = false;
            STARK_LOGW("subghzapp", "save refused: %s", stark_err_str(err));
        }
    }
}

const char *sgfile_saved_name(void)
{
    return s_saved;
}

void sgfile_forget_save(void)
{
    s_saved[0] = '\0';
}

/* ---- list and open ----------------------------------------------------------------------- */

/* Keeps the newest SGAPP_LIST_MAX capture names, highest number first. */
static bool collect(const char *name, uint32_t size, void *ctx)
{
    (void)ctx;
    uint32_t num = subghz_name_number(name);
    size_t at = 0;
    while (at < s_count && subghz_name_number(s_list[at].name) > num) {
        at++;
    }
    if (num == 0 || at == SGAPP_LIST_MAX) {
        return true;
    }
    size_t last = s_count < SGAPP_LIST_MAX ? s_count : SGAPP_LIST_MAX - 1;
    memmove(&s_list[at + 1], &s_list[at], (last - at) * sizeof s_list[0]);
    (void)snprintf(s_list[at].name, sizeof s_list[at].name, "%s", name);
    s_list[at].size = size;
    s_count = s_count < SGAPP_LIST_MAX ? s_count + 1 : SGAPP_LIST_MAX;
    return true;
}

static void list_worker(void *ctx)
{
    (void)ctx;
    s_count = 0;
    stark_err_t err = stark_storage_list(STARK_DIR_CAPTURES, collect, NULL);
    if (err != STARK_OK) {
        s_count = 0;
        STARK_LOGW("subghzapp", "saved: %s", stark_err_str(err));
    }
    notify(SGFILE_NOTE_LISTED);
}

void sgfile_list(void)
{
    stark_storage_state_t state = stark_storage_state();
    if (s_busy) {
        return;
    }
    s_count = 0;
    if (state != STARK_STORAGE_MOUNTED) {
        STARK_LOGI("subghzapp", "saved: %s", stark_storage_state_str(state));
        notify(SGFILE_NOTE_LISTED); /* an empty list says the same on screen */
        return;
    }
    s_busy = true;
    stark_err_t err = stark_app_worker_start(list_worker, NULL);
    if (err != STARK_OK) {
        STARK_LOGW("subghzapp", "saved: %s", stark_err_str(err));
        notify(SGFILE_NOTE_LISTED); /* clears s_busy with the rest of the notifications */
    }
}

const sgfile_entry_t *sgfile_entries(size_t *count)
{
    *count = s_count;
    return s_list;
}

static stark_err_t load_one(const char *name)
{
    subghz_raw_parser_t p;
    subghz_raw_parser_init(&p, s_buf, SUBGHZ_RAW_MAX_SYMBOLS);
    stark_file_t *f = NULL;
    STARK_CHECK_RET(stark_storage_open(STARK_DIR_CAPTURES, name, STARK_FILE_READ, &f));
    char chunk[CHUNK];
    stark_err_t err = STARK_OK;
    size_t got = 1;
    while (err == STARK_OK && got > 0) {
        err = stark_storage_read(f, chunk, sizeof chunk, &got);
        if (err == STARK_OK && got > 0) {
            err = subghz_raw_parser_feed(&p, chunk, got);
        }
    }
    stark_err_t cerr = stark_storage_close(f);
    STARK_CHECK_RET(err);
    STARK_CHECK_RET(cerr);
    return subghz_raw_parser_finish(&p, &s_loaded);
}

static void load_worker(void *ctx)
{
    (void)ctx;
    const char *name = s_list[s_pick].name;
    stark_err_t err = load_one(name);
    if (err != STARK_OK) {
        memset(&s_loaded, 0, sizeof s_loaded);
        STARK_LOGW("subghzapp", "open failed: %s: %s", name, stark_err_str(err));
        notify(SGFILE_NOTE_OPEN_FAILED);
        return;
    }
    subghz_hist_t h;
    char text[128];
    subghz_hist(s_loaded.sym, s_loaded.n, &h);
    sgview_hist_text(&h, text, sizeof text);
    (void)snprintf(s_loaded_name, sizeof s_loaded_name, "%s", name);
    STARK_LOGI("subghzapp", "opened %s (%u sym) hash 0x%08x", name, (unsigned)s_loaded.n,
               (unsigned)subghz_raw_hash(s_loaded.sym, s_loaded.n));
    STARK_LOGI("subghzapp", "hist %s", text);
    notify(SGFILE_NOTE_OPENED);
}

void sgfile_open(size_t index)
{
    if (index >= s_count || s_busy) {
        return;
    }
    if (sgfile_buffer() == NULL) {
        STARK_LOGW("subghzapp", "open failed: no memory");
        return;
    }
    s_pick = index;
    s_busy = true;
    stark_err_t err = stark_app_worker_start(load_worker, NULL);
    if (err != STARK_OK) {
        s_busy = false;
        STARK_LOGW("subghzapp", "open failed: %s", stark_err_str(err));
    }
}

const subghz_raw_t *sgfile_loaded(void)
{
    return &s_loaded;
}

const char *sgfile_loaded_name(void)
{
    return s_loaded_name;
}

void sgfile_forget(void)
{
    s_count = 0;
}
