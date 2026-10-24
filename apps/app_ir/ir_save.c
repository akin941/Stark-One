/*
 * ir_save.c — the IR app's card I/O (TASKS.md STARK-0305/0306): a capture
 * is written as the next free captures/ir_NNNN.ir in the `.ir` text format
 * (docs/ir-format.md), atomically, and a saved file is read back and parsed
 * for Browse — both on the app worker; the card is never touched from the
 * UI task.
 */
#include "ir_names.h"
#include "ir_raw.h"
#include "irapp.h"
#include "stark_app.h"
#include "stark_log.h"
#include "stark_storage.h"

irapp_work_t irapp_work; /* the UI task fills .signal before the worker starts */

static bool scan(const char *name, uint32_t size, void *ctx)
{
    (void)size;
    (void)ir_names_scan(ctx, name);
    return true;
}

static stark_err_t write_one(char *name, size_t len)
{
    ir_names_t names;
    ir_names_init(&names);
    STARK_CHECK_RET(stark_storage_list(STARK_DIR_CAPTURES, scan, &names));
    STARK_CHECK_RET(ir_names_next(&names, name, len));
    size_t n = ir_raw_format(&irapp_work.signal, irapp_work.text, sizeof irapp_work.text);
    if (n == 0) {
        return STARK_ERR_INVALID_ARG;
    }
    stark_file_t *f = NULL;
    STARK_CHECK_RET(stark_storage_open(STARK_DIR_CAPTURES, name, STARK_FILE_WRITE_ATOMIC, &f));
    stark_err_t err = stark_storage_write(f, irapp_work.text, n);
    if (err != STARK_OK) {
        stark_storage_discard(f);
        return err;
    }
    return stark_storage_close(f);
}

static void save_worker(void *ctx)
{
    (void)ctx;
    char name[16];
    stark_err_t err = write_one(name, sizeof name);
    if (err == STARK_OK) {
        STARK_LOGI("irapp", "saved %s", name);
    } else {
        STARK_LOGW("irapp", "save failed: %s", stark_err_str(err));
    }
}

void irapp_save(const ir_signal_t *s)
{
    if (s == NULL) {
        STARK_LOGI("irapp", "save refused: nothing captured");
        return;
    }
    stark_storage_state_t state = stark_storage_state();
    if (state != STARK_STORAGE_MOUNTED) {
        STARK_LOGI("irapp", "save refused: %s", stark_storage_state_str(state));
        return;
    }
    if (irbrowse_busy()) {
        STARK_LOGI("irapp", "save refused: busy"); /* the list worker holds irapp_work */
        return;
    }
    irapp_work.signal = *s;
    stark_err_t err = stark_app_worker_start(save_worker, NULL);
    if (err != STARK_OK) {
        STARK_LOGW("irapp", "save refused: %s", stark_err_str(err));
    }
}

/* On the worker: reads and parses captures/<name> (irapp.h). */
stark_err_t irapp_load(const char *name, ir_signal_t *out)
{
    stark_file_t *f = NULL;
    size_t got = 0;
    STARK_CHECK_RET(stark_storage_open(STARK_DIR_CAPTURES, name, STARK_FILE_READ, &f));
    stark_err_t err = stark_storage_read(f, irapp_work.text, sizeof irapp_work.text, &got);
    stark_err_t cerr = stark_storage_close(f);
    STARK_CHECK_RET(err);
    STARK_CHECK_RET(cerr);
    return ir_raw_parse(irapp_work.text, got, out);
}
