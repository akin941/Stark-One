/*
 * esp_log.h — host UI test port shim (Tier 4B, docs/VALIDATION.md). NOT
 * ESP-IDF: just enough of the esp_log surface for stark_log.h's macros, so
 * production stark_ui/stark_app sources build on the host. Only the UI port
 * targets see this directory; pure-core tests never do (TESTING.md §2).
 * Output is captured by test/host/port/ui_port_log.c for assertions.
 */
#pragma once

/* A plain int (ESP-IDF uses an enum): stark_log.h compares levels with int,
 * and clang would treat a non-negative enum as unsigned (-Wsign-compare). */
typedef int esp_log_level_t;
#define ESP_LOG_NONE    0
#define ESP_LOG_ERROR   1
#define ESP_LOG_WARN    2
#define ESP_LOG_INFO    3
#define ESP_LOG_DEBUG   4
#define ESP_LOG_VERBOSE 5

esp_log_level_t esp_log_level_get(const char *tag);
void esp_log_write(esp_log_level_t level, const char *tag, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
