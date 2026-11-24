/*
 * uart_log.c — the UART app's log to the card (uartapp.h, TASKS.md STARK-0705, ADR-0023 §7): the
 * received bytes appended to logs/uart_NNNN.log through a 2 kB buffer, synced to the medium every 4
 * kB (a close and an append reopen: the storage service syncs on close) and every second a byte
 * waits, at most 1 MiB. At the limit it stops, says so and lets the monitor go on; a card that goes
 * away ends it the same way (`Log stopped`) and costs the bytes still buffered. Everything runs on
 * the app worker, where the card calls may block. The CRC-32 it reports is over exactly the bytes
 * it accepted. Received bytes never reach the console log: it carries names and counts only.
 */
#include <stdlib.h>
#include <string.h>
#include "stark_log.h"
#include "stark_storage.h"
#include "uartapp.h"

#define FLUSH_AFTER_MS 1000u

static struct {
    stark_file_t *file;
    uint8_t *buf;
    size_t fill;
    char name[16];
    uint32_t bytes; /* accepted: buffered or written */
    uint32_t crc;
    uint32_t since_sync; /* bytes written since the last close */
    uint32_t buffered_since_ms;
    bool started; /* a log was started and has not been stopped yet */
    uartlog_state_t state;
} s_log;

static bool scan(const char *name, uint32_t size, void *ctx)
{
    (void)size;
    uint32_t number = 0;
    uint32_t *highest = ctx;
    if (stark_lab_uart_log_name_parse(name, &number) && number > *highest) {
        *highest = number;
    }
    return true;
}

/* Closes the file, which syncs it to the medium; a failure is a log line, the bytes are gone either
 * way. */
static void close_file(void)
{
    stark_file_t *f = s_log.file;
    s_log.file = NULL;
    stark_err_t err = f != NULL ? stark_storage_close(f) : STARK_OK;
    if (err != STARK_OK) {
        STARK_LOGW("uartapp", "log close failed: %s", stark_err_str(err));
    }
}

/* Gives the file and the buffer back; the state says why the log ended. */
static void end(uartlog_state_t state)
{
    if (s_log.file != NULL) {
        stark_storage_discard(s_log.file);
        s_log.file = NULL;
    }
    free(s_log.buf);
    s_log.buf = NULL;
    s_log.fill = 0;
    s_log.state = state;
}

static void fail(stark_err_t err)
{
    STARK_LOGW("uartapp", "log failed: %s", stark_err_str(err));
    end(UARTLOG_FAILED);
}

/* Closes the file (which syncs it) and appends to it again. */
static bool sync_medium(void)
{
    stark_file_t *f = s_log.file;
    s_log.file = NULL;
    stark_err_t err = stark_storage_close(f);
    if (err == STARK_OK) {
        err = stark_storage_open(STARK_DIR_LOGS, s_log.name, STARK_FILE_APPEND, &s_log.file);
    }
    if (err != STARK_OK) {
        fail(err);
        return false;
    }
    s_log.since_sync = 0;
    return true;
}

static bool flush(void)
{
    if (s_log.fill == 0u) {
        return true;
    }
    stark_err_t err = stark_storage_write(s_log.file, s_log.buf, s_log.fill);
    if (err != STARK_OK) {
        fail(err);
        return false;
    }
    s_log.since_sync += (uint32_t)s_log.fill;
    s_log.fill = 0;
    return s_log.since_sync < STARK_LAB_UART_LOG_SYNC || sync_medium();
}

stark_err_t uartlog_start(void)
{
    if (s_log.state == UARTLOG_ON) {
        return STARK_ERR_STATE;
    }
    stark_storage_state_t card = stark_storage_state();
    if (card != STARK_STORAGE_MOUNTED) {
        STARK_LOGI("uartapp", "log refused: %s", stark_storage_state_str(card));
        return STARK_ERR_STATE;
    }
    uint32_t highest = 0;
    char name[16];
    stark_err_t err = stark_storage_list(STARK_DIR_LOGS, scan, &highest);
    if (err == STARK_OK) {
        err = stark_lab_uart_log_name_next(highest, name, sizeof name);
    }
    stark_file_t *f = NULL;
    if (err == STARK_OK) {
        err = stark_storage_open(STARK_DIR_LOGS, name, STARK_FILE_APPEND, &f);
    }
    uint8_t *buf = err == STARK_OK ? malloc(STARK_LAB_UART_LOG_BUF) : NULL;
    err = err == STARK_OK && buf == NULL ? STARK_ERR_NO_MEM : err;
    s_log.file = f; /* end() gives the file and the buffer back if the start failed */
    s_log.buf = buf;
    s_log.fill = 0;
    if (err != STARK_OK) {
        STARK_LOGW("uartapp", "log not started: %s", stark_err_str(err));
        end(UARTLOG_FAILED);
        return err;
    }
    memcpy(s_log.name, name, sizeof s_log.name);
    s_log.bytes = 0;
    s_log.crc = 0;
    s_log.since_sync = 0;
    s_log.started = true;
    s_log.state = UARTLOG_ON;
    STARK_LOGI("uartapp", "log started %s", s_log.name);
    return STARK_OK;
}

void uartlog_write(const uint8_t *data, size_t n, uint32_t now_ms)
{
    if (s_log.state != UARTLOG_ON) {
        return;
    }
    size_t room = STARK_LAB_UART_LOG_MAX - s_log.bytes;
    size_t take = n < room ? n : room;
    size_t done = 0;
    while (done < take) {
        size_t piece = STARK_LAB_UART_LOG_BUF - s_log.fill;
        piece = piece < take - done ? piece : take - done;
        if (s_log.fill == 0u) {
            s_log.buffered_since_ms = now_ms;
        }
        memcpy(s_log.buf + s_log.fill, data + done, piece);
        s_log.fill += piece;
        s_log.bytes += (uint32_t)piece;
        s_log.crc = stark_lab_uart_crc32(s_log.crc, data + done, piece);
        done += piece;
        if (s_log.fill == STARK_LAB_UART_LOG_BUF && !flush()) {
            return;
        }
    }
    if (s_log.bytes >= STARK_LAB_UART_LOG_MAX) {
        if (flush()) {
            STARK_LOGI("uartapp", "log full %s %u bytes", s_log.name, (unsigned)s_log.bytes);
            close_file();
            end(UARTLOG_FULL);
        }
    }
}

void uartlog_tick(uint32_t now_ms)
{
    if (s_log.state == UARTLOG_ON && s_log.fill > 0u &&
        now_ms - s_log.buffered_since_ms >= FLUSH_AFTER_MS) {
        (void)flush();
    }
}

void uartlog_stop(void)
{
    if (!s_log.started) {
        return;
    }
    s_log.started = false;
    if (s_log.state == UARTLOG_ON && flush()) {
        close_file(); /* syncs */
        end(UARTLOG_OFF);
    }
    STARK_LOGI("uartapp", "log closed %s %u bytes crc 0x%08x", s_log.name, (unsigned)s_log.bytes,
               (unsigned)s_log.crc);
    if (s_log.state != UARTLOG_FAILED && s_log.state != UARTLOG_FULL) {
        s_log.state = UARTLOG_OFF;
    }
}

uartlog_state_t uartlog_state(void)
{
    return s_log.state;
}

const char *uartlog_name(void)
{
    return s_log.name;
}

uint32_t uartlog_bytes(void)
{
    return s_log.bytes;
}

uint32_t uartlog_crc(void)
{
    return s_log.crc;
}
