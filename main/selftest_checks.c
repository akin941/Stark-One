/*
 * selftest_checks.c — the self-test checks (selftest_checks.h). Pure over selftest_env_t; the host
 * tests run every one against a healthy and a broken environment, at 100 % of lines.
 */
#include "selftest_checks.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "selftest_sha256.h"
#include "stark_err.h"

#define MIB                  (1024u * 1024u)
#define IMAGE_MAGIC          0xE9u
#define IMAGE_HEADER_BYTES   24u
#define IMAGE_SEGMENT_HEADER 8u
#define IMAGE_SEGMENTS_MAX   16u
#define IMAGE_DIGEST_BYTES   32u
#define RAM_BLOCK_MIN        1024u
#define STORAGE_CHUNK        256u
#define STORAGE_DIR          STARK_DIR_LOGS
#define STORAGE_NAME         "selftest.chk"
#define MODULE_ATTEMPTS      2u

static const selftest_env_t *s_env;

void selftest_checks_bind(const selftest_env_t *env)
{
    s_env = env;
}

#define EVIDENCE(...) ((void)snprintf(evidence, cap, __VA_ARGS__))

/* The environment must be bound before any check runs; a check that finds none says so. */
static bool bound(char *evidence, size_t cap)
{
    if (s_env == NULL) {
        EVIDENCE("no environment");
        return false;
    }
    return true;
}

/* ---- 1. RAM ------------------------------------------------------------------------------- */

/* min(32 kB, a quarter of the largest free block), and never so much that free heap would fall
 * below the floor; a whole number of words; 0 when there is no room for a useful test. */
static size_t ram_block(const selftest_env_t *e)
{
    size_t free_now = e->heap_free();
    if (free_now <= SELFTEST_HEAP_FLOOR) {
        return 0;
    }
    size_t block = e->heap_largest() / 4u;
    if (block > SELFTEST_RAM_BLOCK_MAX) {
        block = SELFTEST_RAM_BLOCK_MAX;
    }
    if (free_now - SELFTEST_HEAP_FLOOR < block) {
        block = free_now - SELFTEST_HEAP_FLOOR;
    }
    return block & ~(size_t)3u;
}

/* A pattern pass: fill the block, give the environment its dwell, verify. `addr` writes each word
 * as its own address (the block's start plus its offset); otherwise every byte is `value`. The
 * first mismatch is returned in *at, *got and *want. Volatile: the compiler may not decide that
 * what was just written is what will be read. */
static bool ram_pass(const selftest_env_t *e, void *buf, size_t n, bool addr, uint8_t value,
                     size_t *at, uint32_t *got, uint32_t *want)
{
    volatile uint32_t *w = buf;
    volatile uint8_t *b = buf;
    if (addr) {
        for (size_t i = 0; i < n / 4u; i++) {
            w[i] = (uint32_t)((uintptr_t)buf + i * 4u);
        }
    } else {
        for (size_t i = 0; i < n; i++) {
            b[i] = value;
        }
    }
    if (e->ram_dwell != NULL) {
        e->ram_dwell(buf, n);
    }
    if (addr) {
        for (size_t i = 0; i < n / 4u; i++) {
            uint32_t expect = (uint32_t)((uintptr_t)buf + i * 4u);
            if (w[i] != expect) {
                *at = i * 4u;
                *got = w[i];
                *want = expect;
                return false;
            }
        }
        return true;
    }
    for (size_t i = 0; i < n; i++) {
        if (b[i] != value) {
            *at = i;
            *got = b[i];
            *want = value;
            return false;
        }
    }
    return true;
}

stark_selftest_result_t selftest_check_ram(char *evidence, size_t cap)
{
    if (!bound(evidence, cap)) {
        return ST_FAIL;
    }
    const selftest_env_t *e = s_env;
    size_t block = ram_block(e);
    if (block < RAM_BLOCK_MIN) {
        EVIDENCE("heap too low: %lu B free", (unsigned long)e->heap_free());
        return ST_FAIL;
    }
    void *buf = e->ram_alloc(block);
    if (buf == NULL) {
        EVIDENCE("no block of %lu B", (unsigned long)block);
        return ST_FAIL;
    }
    stark_selftest_result_t r = ST_PASS;
    if (e->heap_free() < SELFTEST_HEAP_FLOOR) { /* another task took memory meanwhile */
        EVIDENCE("heap fell below 128 kB");
        r = ST_FAIL;
    }
    size_t at = 0;
    uint32_t got = 0;
    uint32_t want = 0;
    /* walking ones, all zeros, all ones, 0xAA, 0x55, address in address */
    static const uint8_t k_bytes[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20,
                                      0x40, 0x80, 0x00, 0xFF, 0xAA, 0x55};
    for (size_t i = 0; r == ST_PASS && i < sizeof k_bytes; i++) {
        if (!ram_pass(e, buf, block, false, k_bytes[i], &at, &got, &want)) {
            EVIDENCE("bad byte +%lu read %02lx want %02lx", (unsigned long)at, (unsigned long)got,
                     (unsigned long)want);
            r = ST_FAIL;
        }
    }
    if (r == ST_PASS && !ram_pass(e, buf, block, true, 0, &at, &got, &want)) {
        EVIDENCE("bad word +%lu read %08lx", (unsigned long)at, (unsigned long)got);
        r = ST_FAIL;
    }
    e->ram_free(buf);
    if (r == ST_PASS) {
        EVIDENCE("%lu B", (unsigned long)block);
    }
    return r;
}

/* ---- 2. Flash ----------------------------------------------------------------------------- */

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

stark_err_t selftest_image_length(const selftest_env_t *env, uint32_t offset, uint32_t limit,
                                  uint32_t *length)
{
    uint8_t hdr[IMAGE_HEADER_BYTES];
    if (env->flash_read(offset, hdr, sizeof hdr) != STARK_OK) {
        return STARK_ERR_IO;
    }
    if (hdr[0] != IMAGE_MAGIC || hdr[1] == 0u || hdr[1] > IMAGE_SEGMENTS_MAX) {
        return STARK_ERR_INVALID_ARG;
    }
    uint32_t pos = IMAGE_HEADER_BYTES;
    for (uint32_t s = 0; s < hdr[1]; s++) {
        uint8_t seg[IMAGE_SEGMENT_HEADER];
        if (pos > limit || limit - pos < sizeof seg) {
            return STARK_ERR_INVALID_ARG;
        }
        if (env->flash_read(offset + pos, seg, sizeof seg) != STARK_OK) {
            return STARK_ERR_IO;
        }
        uint32_t data = le32(seg + 4);
        pos += IMAGE_SEGMENT_HEADER;
        if (data > limit - pos) {
            return STARK_ERR_INVALID_ARG;
        }
        pos += data;
    }
    /* The checksum byte is the last of its 16-byte row; a digest follows when the header says. */
    uint32_t total = (pos | 15u) + 1u;
    if (hdr[IMAGE_HEADER_BYTES - 1u] == 1u) {
        total += IMAGE_DIGEST_BYTES;
    }
    if (total > limit) {
        return STARK_ERR_INVALID_ARG;
    }
    *length = total;
    return STARK_OK;
}

/* The chip size a header's size nibble names, in bytes; 0 for a code this firmware does not know.
 */
static uint32_t size_from_code(uint8_t code)
{
    return code <= 5u ? (MIB << code) : 0u; /* 1 MB .. 32 MB; larger parts are not on this board */
}

/* One read-only SHA-256 pass over [offset, offset + length). */
static stark_err_t hash_image(const selftest_env_t *e, uint32_t offset, uint32_t length,
                              uint8_t digest[32])
{
    selftest_sha256_t ctx;
    selftest_sha256_init(&ctx);
    uint8_t chunk[SELFTEST_FLASH_CHUNK];
    for (uint32_t done = 0; done < length;) {
        size_t n = length - done < sizeof chunk ? (size_t)(length - done) : sizeof chunk;
        stark_err_t err = e->flash_read(offset + done, chunk, n);
        if (err != STARK_OK) {
            return err;
        }
        selftest_sha256_update(&ctx, chunk, n);
        done += (uint32_t)n;
    }
    selftest_sha256_final(&ctx, digest);
    return STARK_OK;
}

stark_selftest_result_t selftest_check_flash(char *evidence, size_t cap)
{
    if (!bound(evidence, cap)) {
        return ST_FAIL;
    }
    const selftest_env_t *e = s_env;
    uint32_t chip = 0;
    if (e->flash_chip_size(&chip) != STARK_OK) {
        EVIDENCE("chip size unreadable");
        return ST_FAIL;
    }
    /* NOTE: the bootloader's header at offset 0 carries the flash size the image was built for
     * (byte 3, upper nibble); the chip the driver detected must be that size. */
    uint8_t hdr[4];
    if (e->flash_read(0, hdr, sizeof hdr) != STARK_OK) {
        EVIDENCE("header unreadable");
        return ST_FAIL;
    }
    if (hdr[0] != IMAGE_MAGIC) {
        EVIDENCE("no image header at 0");
        return ST_FAIL;
    }
    uint32_t declared = size_from_code((uint8_t)(hdr[3] >> 4));
    if (declared == 0u) {
        EVIDENCE("unknown size code %u", (unsigned)(hdr[3] >> 4));
        return ST_FAIL;
    }
    if (declared != chip) {
        EVIDENCE("chip %lu MB, header %lu MB", (unsigned long)(chip / MIB),
                 (unsigned long)(declared / MIB));
        return ST_FAIL;
    }
    uint32_t app_off = 0;
    uint32_t app_size = 0;
    uint32_t length = 0;
    if (e->flash_app(&app_off, &app_size) != STARK_OK) {
        EVIDENCE("no running partition");
        return ST_FAIL;
    }
    if (selftest_image_length(e, app_off, app_size, &length) != STARK_OK) {
        EVIDENCE("image header invalid");
        return ST_FAIL;
    }
    uint8_t first[32];
    uint8_t second[32];
    if (hash_image(e, app_off, length, first) != STARK_OK ||
        hash_image(e, app_off, length, second) != STARK_OK) {
        EVIDENCE("flash read failed");
        return ST_FAIL;
    }
    if (memcmp(first, second, sizeof first) != 0) {
        EVIDENCE("two passes differ");
        return ST_FAIL;
    }
    EVIDENCE("%lu MB, image %lu B, sha %02x%02x%02x%02x", (unsigned long)(chip / MIB),
             (unsigned long)length, first[0], first[1], first[2], first[3]);
    return ST_PASS;
}

/* ---- 3. Settings -------------------------------------------------------------------------- */

stark_selftest_result_t selftest_check_settings(char *evidence, size_t cap)
{
    if (!bound(evidence, cap)) {
        return ST_FAIL;
    }
    const selftest_env_t *e = s_env;
    stark_settings_source_t source = e->settings_source();
    if (source == STARK_SETTINGS_FROM_RAM) {
        EVIDENCE("store unavailable, RAM only");
        return ST_FAIL;
    }
    for (int k = 0; k < (int)STARK_SET_COUNT; k++) {
        const stark_setting_desc_t *d = e->setting_desc((stark_setting_t)k);
        if (d == NULL) {
            EVIDENCE("setting %d has no descriptor", k);
            return ST_FAIL;
        }
        uint8_t v = e->setting_get((stark_setting_t)k);
        if (e->setting_get((stark_setting_t)k) != v) {
            EVIDENCE("%s reads back differently", d->name);
            return ST_FAIL;
        }
        if (v < d->min || v > d->max || (d->step != 0u && (v - d->min) % d->step != 0)) {
            EVIDENCE("%s=%u is invalid", d->name, (unsigned)v);
            return ST_FAIL;
        }
    }
    EVIDENCE("%d settings valid, from %s", (int)STARK_SET_COUNT,
             source == STARK_SETTINGS_FROM_NVS ? "nvs" : "defaults");
    return ST_PASS;
}

/* ---- 4. Storage --------------------------------------------------------------------------- */

static void storage_pattern(uint8_t *chunk, size_t index)
{
    for (size_t j = 0; j < STORAGE_CHUNK; j++) {
        chunk[j] = (uint8_t)(index * 31u + j * 7u + 0x5Au);
    }
}

/* Writes the temp file whole (atomic: dropped unless it closes), or says which step failed. */
static bool storage_write_file(const selftest_env_t *e, char *evidence, size_t cap)
{
    stark_file_t *f = NULL;
    stark_err_t err = e->storage_open(STORAGE_DIR, STORAGE_NAME, STARK_FILE_WRITE_ATOMIC, &f);
    if (err != STARK_OK) {
        EVIDENCE("open failed: %s", stark_err_str(err));
        return false;
    }
    uint8_t chunk[STORAGE_CHUNK];
    for (size_t i = 0; i < SELFTEST_STORAGE_BYTES / STORAGE_CHUNK; i++) {
        storage_pattern(chunk, i);
        err = e->storage_write(f, chunk, sizeof chunk);
        if (err != STARK_OK) {
            e->storage_discard(f);
            EVIDENCE("write failed: %s", stark_err_str(err));
            return false;
        }
    }
    err = e->storage_close(f);
    if (err != STARK_OK) {
        (void)e->storage_remove(STORAGE_DIR, STORAGE_NAME);
        EVIDENCE("close failed: %s", stark_err_str(err));
        return false;
    }
    return true;
}

/* Reads it back and compares; true when every byte is the pattern and nothing is left over. */
static bool storage_verify_file(const selftest_env_t *e, char *evidence, size_t cap)
{
    stark_file_t *f = NULL;
    stark_err_t err = e->storage_open(STORAGE_DIR, STORAGE_NAME, STARK_FILE_READ, &f);
    if (err != STARK_OK) {
        EVIDENCE("reopen failed: %s", stark_err_str(err));
        return false;
    }
    uint8_t want[STORAGE_CHUNK];
    uint8_t got[STORAGE_CHUNK];
    bool ok = true;
    for (size_t i = 0; ok && i < SELFTEST_STORAGE_BYTES / STORAGE_CHUNK; i++) {
        storage_pattern(want, i);
        size_t have = 0;
        while (ok && have < sizeof got) {
            size_t n = 0;
            err = e->storage_read(f, got + have, sizeof got - have, &n);
            if (err != STARK_OK) {
                EVIDENCE("read failed: %s", stark_err_str(err));
                ok = false;
            } else if (n == 0u) {
                EVIDENCE("short read at %lu", (unsigned long)(i * STORAGE_CHUNK + have));
                ok = false;
            } else {
                have += n;
            }
        }
        if (ok && memcmp(want, got, sizeof got) != 0) {
            EVIDENCE("read-back differs in block %lu", (unsigned long)i);
            ok = false;
        }
    }
    (void)e->storage_close(f);
    return ok;
}

stark_selftest_result_t selftest_check_storage(char *evidence, size_t cap)
{
    if (!bound(evidence, cap)) {
        return ST_FAIL;
    }
    const selftest_env_t *e = s_env;
    stark_storage_state_t state = e->storage_state();
    if (state != STARK_STORAGE_MOUNTED) {
        /* NOTE: no card, a board without a slot and a card the user ejected are not faults: skip,
         * with the state. A card that was mounted and went away (REMOVED), or one that answers and
         * holds no usable volume (UNUSABLE), is what the status bar shows as an error, so it fails.
         */
        EVIDENCE("%s", e->storage_state_text(state));
        return state == STARK_STORAGE_REMOVED || state == STARK_STORAGE_UNUSABLE ? ST_FAIL
                                                                                 : ST_SKIP;
    }
    if (!storage_write_file(e, evidence, cap)) {
        return ST_FAIL;
    }
    bool ok = storage_verify_file(e, evidence, cap);
    stark_err_t err = e->storage_remove(STORAGE_DIR, STORAGE_NAME);
    if (err != STARK_OK) {
        if (ok) {
            EVIDENCE("delete failed: %s", stark_err_str(err));
        }
        return ST_FAIL;
    }
    if (ok) {
        EVIDENCE("%lu B written, read back, deleted", (unsigned long)SELFTEST_STORAGE_BYTES);
    }
    return ok ? ST_PASS : ST_FAIL;
}

/* ---- 5. Module bus ------------------------------------------------------------------------ */

/* One look: every slot that has an ID EEPROM address, the table against an address probe. */
static stark_selftest_result_t modules_once(const selftest_env_t *e, char *evidence, size_t cap,
                                            bool *bus_fault)
{
    stark_module_entry_t entries[STARK_MODULE_ENTRIES];
    size_t n = e->module_list(entries, STARK_MODULE_ENTRIES);
    if (n > STARK_MODULE_ENTRIES) {
        n = STARK_MODULE_ENTRIES;
    }
    unsigned slots = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t addr = 0;
        if (entries[i].kind != STARK_MODULE_SLOT ||
            e->eeprom_addr(entries[i].slot, &addr) != STARK_OK) {
            continue;
        }
        stark_err_t err = e->i2c_probe(addr);
        if (err == STARK_ERR_STATE || err == STARK_ERR_NOT_SUPPORTED) {
            EVIDENCE("no I2C bus");
            return ST_SKIP;
        }
        if (err != STARK_OK && err != STARK_ERR_NOT_FOUND) {
            *bus_fault = true;
            EVIDENCE("probe 0x%02x failed: %s", (unsigned)addr, stark_err_str(err));
            return ST_FAIL;
        }
        bool ack = err == STARK_OK;
        bool present = entries[i].state != STARK_MODULE_ABSENT;
        if (present != ack) {
            if (present) {
                EVIDENCE("slot %u present, 0x%02x no ACK", (unsigned)entries[i].slot,
                         (unsigned)addr);
            } else {
                EVIDENCE("slot %u empty, 0x%02x ACKs", (unsigned)entries[i].slot, (unsigned)addr);
            }
            return ST_FAIL;
        }
        slots++;
    }
    if (slots == 0u) {
        EVIDENCE("no slot with an ID EEPROM");
        return ST_SKIP;
    }
    EVIDENCE("table agrees with probes, %u slots", slots);
    return ST_PASS;
}

/* NOTE: a second look is taken when the first disagrees, because the table is a snapshot the
 * module task refreshes once a second and a module may have been fitted a moment ago. A bus
 * fault is not retried: it is the finding. */
stark_selftest_result_t selftest_check_modules(char *evidence, size_t cap)
{
    if (!bound(evidence, cap)) {
        return ST_FAIL;
    }
    stark_selftest_result_t r = ST_FAIL;
    bool bus_fault = false;
    for (unsigned attempt = 0; attempt < MODULE_ATTEMPTS && r == ST_FAIL && !bus_fault; attempt++) {
        r = modules_once(s_env, evidence, cap, &bus_fault);
    }
    return r;
}

/* ---- 6. Events and stacks ----------------------------------------------------------------- */

#define EVENTS_TASKS_MAX 16u

stark_selftest_result_t selftest_check_events(char *evidence, size_t cap)
{
    if (!bound(evidence, cap)) {
        return ST_FAIL;
    }
    const selftest_env_t *e = s_env;
    stark_diag_snapshot_t snap;
    if (e->diag_snapshot(&snap) != STARK_OK) {
        EVIDENCE("no diagnostics snapshot");
        return ST_FAIL;
    }
    if (snap.ev_dropped != 0u) {
        EVIDENCE("%lu events dropped", (unsigned long)snap.ev_dropped);
        return ST_FAIL;
    }
    stark_diag_task_t tasks[EVENTS_TASKS_MAX];
    size_t n = e->diag_tasks(tasks, EVENTS_TASKS_MAX);
    if (n == 0u) {
        EVIDENCE("no task table");
        return ST_FAIL;
    }
    uint32_t least = UINT32_MAX;
    for (size_t i = 0; i < n; i++) {
        if (tasks[i].stack_free < SELFTEST_STACK_MIN) {
            EVIDENCE("%.15s stack %lu B", tasks[i].name, (unsigned long)tasks[i].stack_free);
            return ST_FAIL;
        }
        if (tasks[i].stack_free < least) {
            least = tasks[i].stack_free;
        }
    }
    EVIDENCE("drops 0, %u tasks, min stack %lu B", (unsigned)n, (unsigned long)least);
    return ST_PASS;
}

/* ---- the table ---------------------------------------------------------------------------- */

static const stark_selftest_check_t k_checks[SELFTEST_COUNT] = {
    [SELFTEST_RAM] = {"RAM", false, selftest_check_ram},
    [SELFTEST_FLASH] = {"Flash", false, selftest_check_flash},
    [SELFTEST_SETTINGS] = {"Settings", false, selftest_check_settings},
    [SELFTEST_STORAGE] = {"Storage", false, selftest_check_storage},
    [SELFTEST_MODULES] = {"Module bus", false, selftest_check_modules},
    [SELFTEST_EVENTS] = {"Events and stacks", false, selftest_check_events},
    [SELFTEST_BUTTONS] = {"Buttons", false, NULL},
    [SELFTEST_DISPLAY] = {"Display", true, NULL},
    [SELFTEST_BUZZER] = {"Buzzer", true, NULL},
    [SELFTEST_LED] = {"Status LED", true, NULL},
};

_Static_assert(SELFTEST_COUNT <= STARK_SELFTEST_MAX, "the table fits the runner");

const stark_selftest_check_t *selftest_checks_table(size_t *n)
{
    if (n != NULL) {
        *n = SELFTEST_COUNT;
    }
    return k_checks;
}
