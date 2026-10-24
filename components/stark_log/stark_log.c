/*
 * stark_log.c — logging facade implementation, and the capture ring the
 * storage service drains into the log file (TASKS.md STARK-0206).
 */
#include "stark_log.h"
#include <stdarg.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_ring.h"
#include "sdkconfig.h"

#define CAPTURE_LINE_MAX 160 /* a captured line is truncated past this */

static char s_ring_buf[CONFIG_STARK_LOG_RING_BYTES > 0 ? CONFIG_STARK_LOG_RING_BYTES : 1];
static log_ring_t s_ring;
static portMUX_TYPE s_ring_lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_console; /* esp_log's own output (UART) */

/* Every log line passes here (any task): to the console unchanged, and a
 * copy into the ring. No allocation and no blocking — a spinlock and a copy
 * of at most CAPTURE_LINE_MAX bytes — so logging never waits for the card. */
static int capture_vprintf(const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    int ret = s_console(fmt, args);
    char line[CAPTURE_LINE_MAX];
    int n = vsnprintf(line, sizeof line, fmt, copy);
    va_end(copy);
    if (n > 0) {
        size_t len = (size_t)n < sizeof line ? (size_t)n : sizeof line - 1;
        if ((size_t)n >= sizeof line) {
            line[len - 1] = '\n'; /* truncated: still one whole line */
        }
        portENTER_CRITICAL(&s_ring_lock);
        log_ring_put(&s_ring, line, len);
        portEXIT_CRITICAL(&s_ring_lock);
    }
    return ret;
}

void stark_log_init(void)
{
    /* esp_log itself is initialised by ESP-IDF before app_main. */
    if (CONFIG_STARK_LOG_RING_BYTES > 0 && s_console == NULL) {
        log_ring_init(&s_ring, s_ring_buf, sizeof s_ring_buf);
        s_console = esp_log_set_vprintf(capture_vprintf);
    }
}

size_t stark_log_ring_take(char *buf, size_t max, uint32_t *dropped)
{
    if (buf == NULL || s_console == NULL) {
        if (dropped != NULL) {
            *dropped = 0;
        }
        return 0;
    }
    portENTER_CRITICAL(&s_ring_lock);
    size_t n = log_ring_take(&s_ring, buf, max, dropped);
    portEXIT_CRITICAL(&s_ring_lock);
    return n;
}

void stark_log_set_level(const char *tag, int level)
{
    esp_log_level_set(tag, level);
}

void stark_panic(const char *what, stark_err_t err)
{
    STARK_LOGE("panic", "%s failed: %s - rebooting in 5 s", what, stark_err_str(err));
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}
