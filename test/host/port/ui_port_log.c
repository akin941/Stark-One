/*
 * ui_port_log.c — host UI test port: esp_log capture + stark_log_set_level.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "stark_log.h"
#include "ui_port.h"

#define LOG_LINES 512
#define LOG_LEN   160
#define TAGS      16

static char s_lines[LOG_LINES][LOG_LEN];
static size_t s_count;
static struct {
    const char *tag;
    esp_log_level_t level;
} s_levels[TAGS];
static size_t s_level_count;

esp_log_level_t esp_log_level_get(const char *tag)
{
    for (size_t i = 0; i < s_level_count; i++) {
        if (strcmp(s_levels[i].tag, tag) == 0) {
            return s_levels[i].level;
        }
    }
    return ESP_LOG_INFO; /* the firmware default (sdkconfig.defaults) */
}

void stark_log_set_level(const char *tag, int level)
{
    for (size_t i = 0; i < s_level_count; i++) {
        if (strcmp(s_levels[i].tag, tag) == 0) {
            s_levels[i].level = (esp_log_level_t)level;
            return;
        }
    }
    if (s_level_count < TAGS) {
        s_levels[s_level_count].tag = tag;
        s_levels[s_level_count].level = (esp_log_level_t)level;
        s_level_count++;
    }
}

void esp_log_write(esp_log_level_t level, const char *tag, const char *format, ...)
{
    (void)level;
    (void)tag;
    if (s_count == LOG_LINES) {
        return;
    }
    va_list ap;
    va_start(ap, format);
    (void)vsnprintf(s_lines[s_count], LOG_LEN, format, ap);
    va_end(ap);
    s_lines[s_count][strcspn(s_lines[s_count], "\n")] = '\0';
    s_count++;
}

size_t ui_port_log_count(void)
{
    return s_count;
}

const char *ui_port_log_line(size_t i)
{
    return i < s_count ? s_lines[i] : "";
}

bool ui_port_log_contains(const char *needle)
{
    for (size_t i = 0; i < s_count; i++) {
        if (strstr(s_lines[i], needle) != NULL) {
            return true;
        }
    }
    return false;
}

void ui_port_log_clear(void);
void ui_port_log_clear(void)
{
    s_count = 0;
}
