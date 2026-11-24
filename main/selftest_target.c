/*
 * selftest_target.c — binds the self-test checks and the report export to the real services
 * (selftest_target.h). Target-only.
 *
 * NOTE: the flash is read here and nowhere written or erased: scripts/check.sh (selftest-flash-
 * readonly) fails the build if a selftest file names a flash or partition write.
 */
#include "selftest_target.h"
#include <stdio.h>
#include <string.h>
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "selftest_checks.h"
#include "selftest_env.h"
#include "selftest_export.h"
#include "stark_board.h"
#include "stark_diag.h"
#include "stark_hal_esp.h"
#include "stark_log.h"
#include "stark_module.h"
#include "stark_module_transport.h"
#include "stark_settings.h"
#include "stark_storage.h"
#include "stark_version.h"

#define HEAP_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

static size_t heap_free(void)
{
    return heap_caps_get_free_size(HEAP_CAPS);
}

static size_t heap_largest(void)
{
    return heap_caps_get_largest_free_block(HEAP_CAPS);
}

static void *ram_alloc(size_t n)
{
    return heap_caps_malloc(n, HEAP_CAPS);
}

static void ram_free(void *p)
{
    heap_caps_free(p);
}

static stark_err_t flash_chip_size(uint32_t *bytes)
{
    return stark_err_from_esp(esp_flash_get_size(NULL, bytes));
}

static stark_err_t flash_app(uint32_t *offset, uint32_t *size)
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    if (p == NULL) {
        return STARK_ERR_NOT_FOUND;
    }
    *offset = p->address;
    *size = p->size;
    return STARK_OK;
}

static stark_err_t flash_read(uint32_t offset, void *buf, size_t n)
{
    return stark_err_from_esp(esp_flash_read(NULL, buf, offset, n));
}

static stark_err_t eeprom_addr(uint8_t slot, uint8_t *addr)
{
    if (slot >= STARK_BOARD_MODULE_SLOTS) {
        return STARK_ERR_NOT_FOUND;
    }
    *addr = stark_board_pins()->module[slot].eeprom_addr;
    return *addr != 0u ? STARK_OK : STARK_ERR_NOT_FOUND;
}

static stark_err_t i2c_probe(uint8_t addr)
{
    return stark_module_transport_probe(addr, SELFTEST_PROBE_TIMEOUT_MS);
}

/* One text line per module-table entry: where it is, its state, its name and its descriptor. */
static size_t module_lines(char (*lines)[SELFTEST_MODULE_LINE], size_t max)
{
    static const char *const k_kind[] = {"onboard", "bus", "slot"};
    static const char *const k_state[] = {"absent", "known", "generic", "conflict"};
    stark_module_entry_t entries[STARK_MODULE_ENTRIES];
    size_t n = stark_module_list(entries, STARK_MODULE_ENTRIES);
    if (n > max) {
        n = max;
    }
    for (size_t i = 0; i < n; i++) {
        const stark_module_entry_t *e = &entries[i];
        (void)snprintf(lines[i], SELFTEST_MODULE_LINE, "%s %u %s %s id=0x%04x rev=%u caps=0x%08lx",
                       k_kind[e->kind], (unsigned)e->slot, k_state[e->state], e->name,
                       (unsigned)e->id, (unsigned)e->rev, (unsigned long)e->confirmed);
    }
    return n;
}

static stark_err_t do_export(char *name, size_t cap, size_t *bytes);

static selftest_env_t s_env = {
    .heap_free = heap_free,
    .heap_largest = heap_largest,
    .ram_alloc = ram_alloc,
    .ram_free = ram_free,
    .flash_chip_size = flash_chip_size,
    .flash_app = flash_app,
    .flash_read = flash_read,
    .setting_get = stark_settings_get,
    .setting_desc = stark_settings_desc,
    .settings_source = stark_settings_source,
    .storage_state = stark_storage_state,
    .storage_state_text = stark_storage_state_str,
    .storage_open = stark_storage_open,
    .storage_read = stark_storage_read,
    .storage_write = stark_storage_write,
    .storage_close = stark_storage_close,
    .storage_discard = stark_storage_discard,
    .storage_remove = stark_storage_remove,
    .storage_list = stark_storage_list,
    .module_list = stark_module_list,
    .eeprom_addr = eeprom_addr,
    .i2c_probe = i2c_probe,
    .diag_snapshot = stark_diag_snapshot,
    .diag_tasks = stark_diag_tasks,
    .version = STARK_FIRMWARE_VERSION,
    .build = STARK_BUILD_TIMESTAMP,
    .module_lines = module_lines,
    .log_tail = stark_log_ring_tail,
    .results = stark_selftest_results,
};

static stark_err_t do_export(char *name, size_t cap, size_t *bytes)
{
    return selftest_export_run(&s_env, name, cap, bytes);
}

stark_err_t selftest_target_init(void)
{
    s_env.board = stark_board_name();
    selftest_checks_bind(&s_env);
    size_t n = 0;
    const stark_selftest_check_t *table = selftest_checks_table(&n);
    stark_err_t err = stark_selftest_register(table, n);
    if (err == STARK_OK) {
        stark_selftest_set_export(do_export);
    }
    return err;
}
