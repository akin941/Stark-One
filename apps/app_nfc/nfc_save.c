/*
 * nfc_save.c — the NFC app's dump to the card (TASKS.md STARK-0405,
 * docs/nfc-format.md §2): the NFC task reads the tag into s_dump, then the
 * app worker writes it as the next free captures/nfc_NNNN.nfc, atomically,
 * streaming the text through a small buffer. Inspection only: nothing reads
 * a dump back onto a tag (ADR-0020 §5).
 */
#include <stdatomic.h>
#include "nfcapp.h"
#include "stark_app.h"
#include "stark_log.h"
#include "stark_storage.h"

static nfc_dump_t s_dump;  /* the NFC task fills it, then the worker reads it */
static atomic_bool s_busy; /* a dump or its save holds s_dump (UI task and worker) */

static bool scan(const char *name, uint32_t size, void *ctx)
{
    (void)size;
    (void)nfc_dump_name_scan(ctx, name);
    return true;
}

static stark_err_t write_dump(char *name, size_t len)
{
    uint32_t highest = 0;
    STARK_CHECK_RET(stark_storage_list(STARK_DIR_CAPTURES, scan, &highest));
    STARK_CHECK_RET(nfc_dump_name_next(highest, name, len));
    stark_file_t *f = NULL;
    STARK_CHECK_RET(stark_storage_open(STARK_DIR_CAPTURES, name, STARK_FILE_WRITE_ATOMIC, &f));
    char chunk[256];
    size_t cursor = 0;
    size_t n;
    while ((n = nfc_dump_format(&s_dump, &cursor, chunk, sizeof chunk)) > 0) {
        stark_err_t err = stark_storage_write(f, chunk, n);
        if (err != STARK_OK) {
            stark_storage_discard(f);
            return err;
        }
    }
    return stark_storage_close(f);
}

static void save_worker(void *ctx)
{
    (void)ctx;
    char name[16];
    stark_err_t err = write_dump(name, sizeof name);
    if (err == STARK_OK) {
        STARK_LOGI("nfcapp", "saved %s", name);
    } else {
        STARK_LOGW("nfcapp", "save failed: %s", stark_err_str(err));
    }
    atomic_store(&s_busy, false);
}

void nfcsave_stop(void)
{
    atomic_store(&s_busy, false); /* the worker is joined before on_stop */
}

void nfcsave_request(void)
{
    stark_storage_state_t state = stark_storage_state();
    if (state != STARK_STORAGE_MOUNTED) {
        STARK_LOGI("nfcapp", "save refused: %s", stark_storage_state_str(state));
        return;
    }
    if (atomic_load(&s_busy)) {
        STARK_LOGI("nfcapp", "save refused: busy");
        return;
    }
    stark_err_t err = stark_nfc_request_dump(&s_dump);
    if (err != STARK_OK) {
        STARK_LOGW("nfcapp", "save refused: %s", stark_err_str(err));
        return;
    }
    atomic_store(&s_busy, true);
}

void nfcsave_event(const stark_event_t *e)
{
    if (e->type != STARK_EVT_SYSTEM || e->system.id != STARK_SYS_NFC || !atomic_load(&s_busy)) {
        return;
    }
    if (e->system.arg == STARK_NFC_EVT_DUMP_FAILED) {
        STARK_LOGW("nfcapp", "save failed: %s", stark_err_str(stark_nfc_dump_result()));
        atomic_store(&s_busy, false);
    } else if (e->system.arg == STARK_NFC_EVT_DUMP_DONE) {
        stark_err_t err = stark_app_worker_start(save_worker, NULL);
        if (err != STARK_OK) {
            STARK_LOGW("nfcapp", "save failed: %s", stark_err_str(err));
            atomic_store(&s_busy, false);
        }
    }
}
