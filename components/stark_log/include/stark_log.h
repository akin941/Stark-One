/*
 * stark_log.h — L1 logging facade over esp_log
 * All firmware code uses STARK_LOG* macros instead of ESP_LOG* directly.
 * This allows adding a file sink at V0.2 without touching call sites.
 */
#pragma once

#include "stark_err.h"
#include "esp_log.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Initializes the logging subsystem.
 * Must be called before any STARK_LOG* macro.
 * Sets the default log level from Kconfig (CONFIG_LOG_DEFAULT_LEVEL).
 */
void stark_log_init(void);

/**
 * Sets the log level for a specific tag.
 * @param tag The log tag (e.g. "board", "ui", "input")
 * @param level One of ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE
 */
void stark_log_set_level(const char *tag, int level);

/*
 * Logging macros — mirror ESP_LOG* but with stark_ prefix.
 * Tags are per-component, lowercase snake_case: "board", "ui", "input", "event", etc.
 *
 * Usage:
 *   STARK_LOGI("board", "SPI bus initialized at %d Hz", freq_hz);
 *   STARK_LOGE("input", "Failed to configure GPIO %d", pin);
 */

/*
 * esp_log_write() adds no formatting of its own — no tag prefix, no
 * timestamp, no trailing newline (see esp_log_write.h). The format string
 * passed to it must already be complete, so the facade prepends the tag
 * and appends "\n" here, once, rather than at every call site.
 */
#define STARK_LOGV(tag, fmt, ...) \
    do { \
        if (stark_log_level_allowed(tag, ESP_LOG_VERBOSE)) { \
            esp_log_write(ESP_LOG_VERBOSE, tag, "%s: " fmt "\n", tag, ##__VA_ARGS__); \
        } \
    } while (0)

#define STARK_LOGD(tag, fmt, ...) \
    do { \
        if (stark_log_level_allowed(tag, ESP_LOG_DEBUG)) { \
            esp_log_write(ESP_LOG_DEBUG, tag, "%s: " fmt "\n", tag, ##__VA_ARGS__); \
        } \
    } while (0)

#define STARK_LOGI(tag, fmt, ...) \
    do { \
        if (stark_log_level_allowed(tag, ESP_LOG_INFO)) { \
            esp_log_write(ESP_LOG_INFO, tag, "%s: " fmt "\n", tag, ##__VA_ARGS__); \
        } \
    } while (0)

#define STARK_LOGW(tag, fmt, ...) \
    do { \
        if (stark_log_level_allowed(tag, ESP_LOG_WARN)) { \
            esp_log_write(ESP_LOG_WARN, tag, "%s: " fmt "\n", tag, ##__VA_ARGS__); \
        } \
    } while (0)

#define STARK_LOGE(tag, fmt, ...) \
    do { \
        if (stark_log_level_allowed(tag, ESP_LOG_ERROR)) { \
            esp_log_write(ESP_LOG_ERROR, tag, "%s: " fmt "\n", tag, ##__VA_ARGS__); \
        } \
    } while (0)

/* Internal helper to check if a log level is enabled for a tag */
static inline bool stark_log_level_allowed(const char *tag, int level)
{
    (void)tag;
    return esp_log_level_get(tag) >= level;
}