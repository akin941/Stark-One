/*
 * storage_wtest.c — the write test (storage_wtest.h): 100 kB of a
 * deterministic pattern (xorshift32, fixed seed) streamed in 4 kB chunks to
 * captures/wtest.bin with an atomic write, then read back and compared byte
 * for byte with the regenerated pattern, CRC-32 of both logged. All on the
 * app worker, so the UI task keeps rendering; the frames it rendered during
 * the test are logged as evidence (a count, never a duration).
 */
#include "storage_wtest.h"
#include <stdbool.h>
#include "stark_app.h"
#include "stark_event.h"
#include "stark_log.h"
#include "stark_storage.h"
#include "stark_ui.h"

#define WTEST_NAME "wtest.bin"
#define WTEST_SEED 0x5354524Bu /* "STRK" */

static uint8_t s_buf[WTEST_CHUNK];

uint32_t storage_wtest_crc32(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    while (n-- > 0) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static void fill(uint32_t *state, uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint32_t x = *state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        *state = x;
        p[i] = (uint8_t)x;
    }
}

static void progress(uint32_t done_bytes, bool reading)
{
    stark_event_t e = {.type = STARK_EVT_APP_REQUEST};
    e.app.id = STARK_APP_REQ_NOTIFY;
    e.app.arg = (int32_t)((reading ? 50u : 0u) + done_bytes * 50u / WTEST_BYTES);
    (void)stark_event_publish(&e);
}

static uint32_t frames(void)
{
    stark_ui_stats_t st = {0};
    (void)stark_ui_stats(&st);
    return st.frames;
}

/* Writes the pattern; on error *at = the failing offset. */
static stark_err_t write_pass(uint32_t *crc, uint32_t *at)
{
    stark_file_t *f = NULL;
    STARK_CHECK_RET(
        stark_storage_open(STARK_DIR_CAPTURES, WTEST_NAME, STARK_FILE_WRITE_ATOMIC, &f));
    uint32_t state = WTEST_SEED;
    for (*at = 0; *at < WTEST_BYTES; *at += WTEST_CHUNK) {
        if (stark_app_worker_should_stop()) {
            stark_storage_discard(f);
            return STARK_ERR_STATE;
        }
        fill(&state, s_buf, WTEST_CHUNK);
        *crc = storage_wtest_crc32(*crc, s_buf, WTEST_CHUNK);
        stark_err_t err = stark_storage_write(f, s_buf, WTEST_CHUNK);
        if (err != STARK_OK) {
            stark_storage_discard(f);
            return err;
        }
        progress(*at + WTEST_CHUNK, false);
    }
    return stark_storage_close(f);
}

/* Reads it back, comparing every byte with the regenerated pattern. */
static stark_err_t read_pass(uint32_t *crc, uint32_t *at)
{
    stark_file_t *f = NULL;
    STARK_CHECK_RET(stark_storage_open(STARK_DIR_CAPTURES, WTEST_NAME, STARK_FILE_READ, &f));
    uint32_t state = WTEST_SEED;
    stark_err_t err = STARK_OK;
    for (*at = 0; err == STARK_OK && *at < WTEST_BYTES; *at += WTEST_CHUNK) {
        size_t got = 0;
        err = stark_storage_read(f, s_buf, WTEST_CHUNK, &got);
        if (err == STARK_OK && got != WTEST_CHUNK) {
            err = STARK_ERR_IO; /* short file */
        }
        for (size_t i = 0; err == STARK_OK && i < WTEST_CHUNK; i++) {
            uint8_t want;
            fill(&state, &want, 1);
            if (s_buf[i] != want) {
                err = STARK_ERR_IO; /* not byte-identical */
                *at += (uint32_t)i;
            }
        }
        if (err == STARK_OK) {
            *crc = storage_wtest_crc32(*crc, s_buf, WTEST_CHUNK);
            progress(*at + WTEST_CHUNK, true);
        }
    }
    stark_err_t close_err = stark_storage_close(f);
    return err != STARK_OK ? err : close_err;
}

static void run(void *ctx)
{
    (void)ctx;
    uint32_t frames0 = frames();
    uint32_t wcrc = 0, rcrc = 0, at = 0;
    stark_err_t err = write_pass(&wcrc, &at);
    if (err == STARK_OK) {
        err = read_pass(&rcrc, &at);
    }
    if (err == STARK_OK && rcrc != wcrc) {
        err = STARK_ERR_IO;
    }
    if (err == STARK_OK) {
        STARK_LOGI("storageapp", "wtest ok bytes=%u crc=%08x frames=%u", (unsigned)WTEST_BYTES,
                   (unsigned)wcrc, (unsigned)(frames() - frames0));
    } else if (err != STARK_ERR_STATE || !stark_app_worker_should_stop()) {
        STARK_LOGW("storageapp", "wtest failed: %s at %u", stark_err_str(err), (unsigned)at);
    }
    progress(err == STARK_OK ? WTEST_BYTES : 0, true);
}

void storage_wtest_start(void)
{
    stark_storage_state_t state = stark_storage_state();
    if (state != STARK_STORAGE_MOUNTED) {
        STARK_LOGI("storageapp", "wtest refused: %s", stark_storage_state_str(state));
        return;
    }
    stark_err_t err = stark_app_worker_start(run, NULL);
    if (err != STARK_OK) {
        STARK_LOGW("storageapp", "wtest not started: %s", stark_err_str(err));
        return;
    }
    STARK_LOGI("storageapp", "wtest start");
}
