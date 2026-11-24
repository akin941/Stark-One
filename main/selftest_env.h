/*
 * selftest_env.h — the composition root's self-test: what the checks and the report export need
 * from the rest of the firmware, as one table of calls (TASKS.md STARK-0707, ADR-0023 §8).
 *
 * selftest_checks.c and selftest_export.c are pure over this table: they include no ESP-IDF
 * header and no service's implementation, so the host tests give them an environment they can
 * make healthy or break one call at a time (a corrupted RAM word, a flash size mismatch, a card
 * that fails at each step, a module that declares and does not ACK, a drop count of one, a stack
 * of 128 B). selftest_target.c fills the same table from the real services and registers the
 * checks (the root may require anything, so stark_diag gains no edge).
 *
 * Nothing here writes to flash: the flash group has a size and a read, and no other call.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "module_desc.h"
#include "stark_diag.h"
#include "stark_err.h"
#include "stark_module.h"
#include "stark_settings.h"
#include "stark_storage.h"

#define SELFTEST_MODULE_LINE 80u /* characters of one module-table line in the report */

typedef struct {
    /* ---- RAM (check 1): internal 8-bit heap only; PSRAM stays unused (ADR-0014) ------------ */
    size_t (*heap_free)(void);
    size_t (*heap_largest)(void);
    void *(*ram_alloc)(size_t n);
    void (*ram_free)(void *p);
    /* NOTE: called between a pattern's write and its verification, the place where a cell that
     * cannot hold its value shows. The firmware leaves it NULL; the host tests flip a bit here. */
    void (*ram_dwell)(void *p, size_t n);

    /* ---- flash (check 2): read-only ---------------------------------------------------------- */
    stark_err_t (*flash_chip_size)(uint32_t *bytes);
    /* The running app partition: its offset and size. */
    stark_err_t (*flash_app)(uint32_t *offset, uint32_t *size);
    stark_err_t (*flash_read)(uint32_t offset, void *buf, size_t n);

    /* ---- settings (3) ------------------------------------------------------------------------ */
    uint8_t (*setting_get)(stark_setting_t key);
    const stark_setting_desc_t *(*setting_desc)(stark_setting_t key);
    stark_settings_source_t (*settings_source)(void);

    /* ---- storage (4, and the report) --------------------------------------------------------- */
    stark_storage_state_t (*storage_state)(void);
    const char *(*storage_state_text)(stark_storage_state_t state);
    stark_err_t (*storage_open)(stark_dir_t dir, const char *name, stark_file_mode_t mode,
                                stark_file_t **out);
    stark_err_t (*storage_read)(stark_file_t *f, void *buf, size_t len, size_t *got);
    stark_err_t (*storage_write)(stark_file_t *f, const void *buf, size_t len);
    stark_err_t (*storage_close)(stark_file_t *f);
    void (*storage_discard)(stark_file_t *f);
    stark_err_t (*storage_remove)(stark_dir_t dir, const char *name);
    stark_err_t (*storage_list)(stark_dir_t dir, stark_storage_list_fn fn, void *ctx);

    /* ---- module bus (5, and the report) ------------------------------------------------------ */
    size_t (*module_list)(stark_module_entry_t *out, size_t max);
    /* The slot's ID EEPROM address; an error for a slot the board does not have. */
    stark_err_t (*eeprom_addr)(uint8_t slot, uint8_t *addr);
    /* An address-only probe: STARK_OK on an ACK, STARK_ERR_NOT_FOUND on a NACK, STARK_ERR_STATE
     * without a bus, anything else is a bus fault. */
    stark_err_t (*i2c_probe)(uint8_t addr);

    /* ---- events and stacks (6, and the report) ----------------------------------------------- */
    stark_err_t (*diag_snapshot)(stark_diag_snapshot_t *out);
    size_t (*diag_tasks)(stark_diag_task_t *out, size_t max);

    /* ---- report ------------------------------------------------------------------------------ */
    const char *version;
    const char *build;
    const char *board;
    /* One text line per module-table entry into lines[i]; returns how many (at most max). */
    size_t (*module_lines)(char (*lines)[SELFTEST_MODULE_LINE], size_t max);
    /* The newest whole lines of the log ring (at most max bytes), left in place. */
    size_t (*log_tail)(char *buf, size_t max);
    size_t (*results)(stark_selftest_row_t *out, size_t max);
} selftest_env_t;
