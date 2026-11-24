/*
 * selftest_env_fake.c — the model behind selftest_env_fake.h. Test-only.
 */
#include "selftest_env_fake.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings_core.h"

fenv_t fenv;

#define HANDLES 2

typedef struct {
    bool used;
    bool write;
    char name[FENV_NAME];
    uint8_t *buf; /* a write's pending content */
    size_t len;
    size_t pos; /* a read's position */
    fenv_file_t *file;
} handle_t;

static handle_t s_handles[HANDLES];
static uint8_t s_pending[HANDLES][FENV_FILE_BYTES];
static void *s_block;
static size_t s_block_size;

/* ---- RAM ---------------------------------------------------------------------------------- */

static size_t f_heap_free(void)
{
    return fenv.free_total - fenv.allocated;
}

static size_t f_heap_largest(void)
{
    return fenv.largest;
}

static void *f_ram_alloc(size_t n)
{
    if (fenv.alloc_fails) {
        return NULL;
    }
    void *p = malloc(n);
    fenv.allocs++;
    fenv.last_alloc = n;
    fenv.allocated += n + fenv.steal_on_alloc;
    if (f_heap_free() < fenv.min_free_seen) {
        fenv.min_free_seen = f_heap_free();
    }
    if (n >= 1024u && s_block == NULL) {
        s_block = p;
        s_block_size = n;
    }
    return p;
}

static void f_ram_free(void *p)
{
    fenv.frees++;
    if (p == s_block) {
        fenv.allocated -= s_block_size;
        s_block = NULL;
    } else {
        fenv.allocated = 0;
    }
    free(p);
}

static void f_ram_dwell(void *p, size_t n)
{
    fenv.dwells++;
    if (fenv.stuck_mask != 0u && fenv.stuck_offset < n &&
        (fenv.stuck_on_call == 0u || fenv.stuck_on_call == fenv.dwells)) {
        ((volatile uint8_t *)p)[fenv.stuck_offset] ^= fenv.stuck_mask;
    }
}

/* ---- flash -------------------------------------------------------------------------------- */

static stark_err_t f_flash_chip_size(uint32_t *bytes)
{
    if (fenv.chip_size_fails) {
        return STARK_ERR_IO;
    }
    *bytes = fenv.chip_size;
    return STARK_OK;
}

static stark_err_t f_flash_app(uint32_t *offset, uint32_t *size)
{
    if (fenv.app_fails) {
        return STARK_ERR_NOT_FOUND;
    }
    *offset = FENV_APP_OFFSET;
    *size = FENV_APP_SIZE;
    return STARK_OK;
}

static stark_err_t f_flash_read(uint32_t offset, void *buf, size_t n)
{
    fenv.reads++;
    fenv.bytes_read += (uint32_t)n;
    if ((uint64_t)offset + n > FENV_FLASH_BYTES) {
        return STARK_ERR_INVALID_ARG;
    }
    if (fenv.read_fail_at >= offset && fenv.read_fail_at < offset + n) {
        return STARK_ERR_IO;
    }
    if (offset + n > fenv.max_offset_read) {
        fenv.max_offset_read = offset + (uint32_t)n;
    }
    memcpy(buf, fenv.flash + offset, n);
    if (fenv.corrupt_after_reads != 0u && fenv.reads > fenv.corrupt_after_reads && n > 0u) {
        ((uint8_t *)buf)[0] ^= 1u;
    }
    return STARK_OK;
}

/* ---- settings ----------------------------------------------------------------------------- */

static uint8_t f_setting_get(stark_setting_t key)
{
    fenv.get_calls++;
    uint8_t v = fenv.setting[key];
    return fenv.unstable ? (uint8_t)(v + (fenv.get_calls & 1u)) : v;
}

static const stark_setting_desc_t *f_setting_desc(stark_setting_t key)
{
    return fenv.no_desc ? NULL : settings_desc(key);
}

static stark_settings_source_t f_settings_source(void)
{
    return fenv.source;
}

/* ---- the card ----------------------------------------------------------------------------- */

static stark_storage_state_t f_storage_state(void)
{
    return fenv.state;
}

static const char *f_storage_state_text(stark_storage_state_t s)
{
    static const char *const k_text[] = {"not fitted", "no card", "mounted",
                                         "unusable",   "removed", "ejected"};
    return (unsigned)s < sizeof k_text / sizeof k_text[0] ? k_text[s] : "?";
}

fenv_file_t *fenv_find(const char *name)
{
    for (int i = 0; i < FENV_FILES; i++) {
        if (fenv.files[i].used && strcmp(fenv.files[i].name, name) == 0) {
            return &fenv.files[i];
        }
    }
    return NULL;
}

unsigned fenv_file_count(void)
{
    unsigned n = 0;
    for (int i = 0; i < FENV_FILES; i++) {
        n += fenv.files[i].used ? 1u : 0u;
    }
    return n;
}

static stark_err_t f_storage_open(stark_dir_t dir, const char *name, stark_file_mode_t mode,
                                  stark_file_t **out)
{
    (void)dir;
    fenv.opens++;
    bool write = mode == STARK_FILE_WRITE_ATOMIC;
    if ((write && fenv.fault == FENV_OPEN_WRITE) || (!write && fenv.fault == FENV_OPEN_READ)) {
        return STARK_ERR_IO;
    }
    fenv_file_t *file = write ? NULL : fenv_find(name);
    if (!write && file == NULL) {
        return STARK_ERR_NOT_FOUND;
    }
    for (int i = 0; i < HANDLES; i++) {
        if (!s_handles[i].used) {
            s_handles[i] =
                (handle_t){.used = true, .write = write, .buf = s_pending[i], .file = file};
            snprintf(s_handles[i].name, sizeof s_handles[i].name, "%s", name);
            *out = (stark_file_t *)&s_handles[i];
            fenv.handles_open++;
            return STARK_OK;
        }
    }
    return STARK_ERR_BUSY;
}

static stark_err_t f_storage_write(stark_file_t *f, const void *buf, size_t len)
{
    handle_t *h = (handle_t *)f;
    unsigned nth = fenv.writes++;
    if (fenv.fault == FENV_WRITE && nth == fenv.fail_nth) {
        return STARK_ERR_IO;
    }
    if (h->len + len > FENV_FILE_BYTES) {
        return STARK_ERR_NO_SPACE;
    }
    memcpy(h->buf + h->len, buf, len);
    h->len += len;
    return STARK_OK;
}

static stark_err_t f_storage_close(stark_file_t *f)
{
    handle_t *h = (handle_t *)f;
    fenv.closes++;
    stark_err_t err = STARK_OK;
    if (h->write) {
        if (fenv.fault == FENV_CLOSE) {
            err = STARK_ERR_IO;
        } else {
            fenv_file_t *file = fenv_find(h->name);
            for (int i = 0; file == NULL && i < FENV_FILES; i++) {
                if (!fenv.files[i].used) {
                    file = &fenv.files[i];
                }
            }
            if (file == NULL) {
                err = STARK_ERR_NO_SPACE;
            } else {
                snprintf(file->name, sizeof file->name, "%s", h->name);
                memcpy(file->data, h->buf, h->len);
                file->len = h->len;
                file->used = true;
            }
        }
    }
    h->used = false;
    fenv.handles_open--;
    return err;
}

static void f_storage_discard(stark_file_t *f)
{
    handle_t *h = (handle_t *)f;
    fenv.discards++;
    h->used = false;
    fenv.handles_open--;
}

static stark_err_t f_storage_read(stark_file_t *f, void *buf, size_t len, size_t *got)
{
    handle_t *h = (handle_t *)f;
    if (fenv.fault == FENV_READ) {
        return STARK_ERR_IO;
    }
    size_t end = h->file->len;
    if (fenv.fault == FENV_SHORT_READ && end > 100u) {
        end -= 100u;
    }
    size_t n = end > h->pos ? end - h->pos : 0u;
    if (n > len) {
        n = len;
    }
    memcpy(buf, h->file->data + h->pos, n);
    if (fenv.fault == FENV_BAD_READ && h->pos <= 300u && h->pos + n > 300u) {
        ((uint8_t *)buf)[300u - h->pos] ^= 1u;
    }
    h->pos += n;
    *got = n;
    return STARK_OK;
}

static stark_err_t f_storage_remove(stark_dir_t dir, const char *name)
{
    (void)dir;
    fenv.removes++;
    if (fenv.fault == FENV_REMOVE) {
        return STARK_ERR_IO;
    }
    fenv_file_t *file = fenv_find(name);
    if (file == NULL) {
        return STARK_ERR_NOT_FOUND;
    }
    file->used = false;
    return STARK_OK;
}

static stark_err_t f_storage_list(stark_dir_t dir, stark_storage_list_fn fn, void *ctx)
{
    (void)dir;
    if (fenv.fault == FENV_LIST) {
        return STARK_ERR_IO;
    }
    for (int i = 0; i < FENV_FILES; i++) {
        if (fenv.files[i].used && !fn(fenv.files[i].name, (uint32_t)fenv.files[i].len, ctx)) {
            break;
        }
    }
    return STARK_OK;
}

/* ---- modules ------------------------------------------------------------------------------ */

static size_t f_module_list(stark_module_entry_t *out, size_t max)
{
    size_t n = fenv.n_entries < max ? fenv.n_entries : max;
    memcpy(out, fenv.entries, n * sizeof out[0]);
    fenv.lists++;
    if (fenv.change_after_list != 0u && fenv.lists == fenv.change_after_list) {
        fenv.entries[0].state = STARK_MODULE_KNOWN; /* a module was fitted a moment ago */
        fenv.probe_result[0] = STARK_OK;
    }
    return fenv.n_entries + fenv.list_overreport;
}

static stark_err_t f_eeprom_addr(uint8_t slot, uint8_t *addr)
{
    if (fenv.no_eeprom_addr || slot >= 2u) {
        return STARK_ERR_NOT_FOUND;
    }
    *addr = (uint8_t)(0x50u + slot);
    return STARK_OK;
}

static stark_err_t f_i2c_probe(uint8_t addr)
{
    fenv.probes++;
    return fenv.probe_result[addr - 0x50u];
}

static size_t f_module_lines(char (*lines)[SELFTEST_MODULE_LINE], size_t max)
{
    size_t n = fenv.n_entries < max ? fenv.n_entries : max;
    for (size_t i = 0; i < n; i++) {
        snprintf(lines[i], SELFTEST_MODULE_LINE, "entry %u state %u %s", (unsigned)i,
                 (unsigned)fenv.entries[i].state, fenv.entries[i].name);
    }
    return n + fenv.modules_overreport;
}

/* ---- diagnostics and the report ----------------------------------------------------------- */

static stark_err_t f_diag_snapshot(stark_diag_snapshot_t *out)
{
    if (fenv.snap_fails) {
        return STARK_ERR_STATE;
    }
    *out = fenv.snap;
    return STARK_OK;
}

static size_t f_diag_tasks(stark_diag_task_t *out, size_t max)
{
    size_t n = fenv.n_tasks < max ? fenv.n_tasks : max;
    memcpy(out, fenv.tasks, n * sizeof out[0]);
    return n;
}

static size_t f_log_tail(char *buf, size_t max)
{
    size_t n = fenv.tail_len < max ? fenv.tail_len : max;
    memcpy(buf, fenv.tail, n);
    return n + fenv.tail_overreport;
}

static size_t f_results(stark_selftest_row_t *out, size_t max)
{
    size_t n = fenv.n_rows < max ? fenv.n_rows : max;
    memcpy(out, fenv.rows, n * sizeof out[0]);
    return fenv.n_rows;
}

selftest_env_t fenv_env = {
    .heap_free = f_heap_free,
    .heap_largest = f_heap_largest,
    .ram_alloc = f_ram_alloc,
    .ram_free = f_ram_free,
    .ram_dwell = f_ram_dwell,
    .flash_chip_size = f_flash_chip_size,
    .flash_app = f_flash_app,
    .flash_read = f_flash_read,
    .setting_get = f_setting_get,
    .setting_desc = f_setting_desc,
    .settings_source = f_settings_source,
    .storage_state = f_storage_state,
    .storage_state_text = f_storage_state_text,
    .storage_open = f_storage_open,
    .storage_read = f_storage_read,
    .storage_write = f_storage_write,
    .storage_close = f_storage_close,
    .storage_discard = f_storage_discard,
    .storage_remove = f_storage_remove,
    .storage_list = f_storage_list,
    .module_list = f_module_list,
    .eeprom_addr = f_eeprom_addr,
    .i2c_probe = f_i2c_probe,
    .diag_snapshot = f_diag_snapshot,
    .diag_tasks = f_diag_tasks,
    .version = "v0.7-test",
    .build = "2026-11-24T00:00:00Z",
    .board = "devkitc1",
    .module_lines = f_module_lines,
    .log_tail = f_log_tail,
    .results = f_results,
};

uint32_t fenv_put_image(unsigned segments, uint32_t segment_bytes, bool digest)
{
    uint8_t *p = fenv.flash + FENV_APP_OFFSET;
    memset(p, 0xFF, FENV_APP_SIZE);
    memset(p, 0, 24);
    p[0] = 0xE9;
    p[1] = (uint8_t)segments;
    p[23] = digest ? 1u : 0u;
    uint32_t pos = 24;
    for (unsigned s = 0; s < segments; s++) {
        p[pos + 0] = 0x00;
        p[pos + 1] = 0x00;
        p[pos + 2] = 0x00;
        p[pos + 3] = 0x3C;
        p[pos + 4] = (uint8_t)segment_bytes;
        p[pos + 5] = (uint8_t)(segment_bytes >> 8);
        p[pos + 6] = (uint8_t)(segment_bytes >> 16);
        p[pos + 7] = (uint8_t)(segment_bytes >> 24);
        for (uint32_t i = 0; i < segment_bytes; i++) {
            p[pos + 8 + i] = (uint8_t)(i * 7u + s);
        }
        pos += 8u + segment_bytes;
    }
    uint32_t checksum_at = pos | 15u;
    for (uint32_t i = pos; i <= checksum_at; i++) {
        p[i] = 0xA5;
    }
    uint32_t total = checksum_at + 1u;
    if (digest) {
        for (uint32_t i = 0; i < 32u; i++) {
            p[total + i] = (uint8_t)(0xD0u + i);
        }
        total += 32u;
    }
    return total;
}

void fenv_reset(void)
{
    memset(&fenv, 0, sizeof fenv);
    memset(s_handles, 0, sizeof s_handles);
    s_block = NULL;
    s_block_size = 0;
    fenv.free_total = 226356;
    fenv.largest = 110000;
    fenv.min_free_seen = SIZE_MAX;
    fenv.chip_size = 4u * 1024u * 1024u;
    fenv.read_fail_at = UINT32_MAX;
    memset(fenv.flash, 0xFF, 0x10000u);
    fenv.flash[0] = 0xE9;
    fenv.flash[1] = 3;
    fenv.flash[3] = 0x20; /* flash size nibble 2: 4 MB */
    (void)fenv_put_image(3, 4000, true);
    for (int k = 0; k < (int)STARK_SET_COUNT; k++) {
        fenv.setting[k] = settings_desc((stark_setting_t)k)->def;
    }
    fenv.source = STARK_SETTINGS_FROM_NVS;
    fenv.state = STARK_STORAGE_MOUNTED;
    fenv.entries[0] =
        (stark_module_entry_t){.kind = STARK_MODULE_SLOT, .state = STARK_MODULE_ABSENT, .slot = 0};
    fenv.entries[1] =
        (stark_module_entry_t){.kind = STARK_MODULE_SLOT, .state = STARK_MODULE_ABSENT, .slot = 1};
    fenv.entries[2] = (stark_module_entry_t){
        .kind = STARK_MODULE_ONBOARD, .state = STARK_MODULE_KNOWN, .name = "IR"};
    fenv.entries[3] = (stark_module_entry_t){
        .kind = STARK_MODULE_BUS, .state = STARK_MODULE_ABSENT, .name = "PN532"};
    fenv.n_entries = 4;
    fenv.probe_result[0] = STARK_ERR_NOT_FOUND;
    fenv.probe_result[1] = STARK_ERR_NOT_FOUND;
    fenv.snap.uptime_s = 12;
    fenv.snap.ev_published = 100;
    fenv.snap.heap_free = 226356;
    fenv.n_tasks = 3;
    snprintf(fenv.tasks[0].name, sizeof fenv.tasks[0].name, "stark_ui");
    fenv.tasks[0].stack_free = 2400;
    snprintf(fenv.tasks[1].name, sizeof fenv.tasks[1].name, "app_worker");
    fenv.tasks[1].stack_free = 1800;
    snprintf(fenv.tasks[2].name, sizeof fenv.tasks[2].name, "IDLE0");
    fenv.tasks[2].stack_free = 700;
    snprintf(fenv.tail, sizeof fenv.tail, "boot: one\nboot: two\nui: three\n");
    fenv.tail_len = strlen(fenv.tail);
    static const char *const k_names[10] = {
        "RAM",     "Flash",   "Settings", "Storage",   "Module bus", "Events and stacks",
        "Buttons", "Display", "Buzzer",   "Status LED"};
    fenv.n_rows = 10;
    for (size_t i = 0; i < 10; i++) {
        fenv.rows[i].name = k_names[i];
        fenv.rows[i].result = i < 7 ? ST_PASS : ST_ATTESTED;
        fenv.rows[i].attested = i >= 7;
        snprintf(fenv.rows[i].evidence, sizeof fenv.rows[i].evidence, "ev %u", (unsigned)i);
    }
}
