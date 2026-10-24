/*
 * app_settings.c — Settings (TASKS.md STARK-0205): brightness, volume, key
 * repeat and log level, each changed with LEFT/RIGHT (or OK, which steps
 * forward and wraps) and applied at once by the composition root; Restore
 * defaults asks first. Leaving the app commits the changes to NVS, so a
 * value survives a reboot. stark_settings logs every change.
 */
#include <stdio.h>
#include "stark_app.h"
#include "stark_settings.h"
#include "ui_dialog.h"
#include "ui_menu.h"

#define ROW_RESTORE STARK_SET_COUNT

static const ui_menu_item_t k_items[] = {
    [STARK_SET_BRIGHTNESS] = {.label = "Brightness"},
    [STARK_SET_VOLUME] = {.label = "Volume"},
    [STARK_SET_REPEAT] = {.label = "Key repeat"},
    [STARK_SET_LOG_LEVEL] = {.label = "Log level"},
    [ROW_RESTORE] = {.label = "Restore defaults"},
};
static ui_menu_t s_menu;
static char s_value[12];

static const char *value_of(size_t index, void *ctx)
{
    (void)ctx;
    if (index >= STARK_SET_COUNT) {
        return NULL;
    }
    const stark_setting_desc_t *d = stark_settings_desc((stark_setting_t)index);
    uint8_t v = stark_settings_get((stark_setting_t)index);
    if (d->labels != NULL) {
        return d->labels[v - d->min];
    }
    snprintf(s_value, sizeof s_value, "%u%%", (unsigned)v);
    return s_value;
}

/* One step in dir, clamped (wrap: past the end goes back to the start). */
static bool step(size_t index, int dir, bool wrap)
{
    if (index >= STARK_SET_COUNT) {
        return false;
    }
    stark_setting_t key = (stark_setting_t)index;
    const stark_setting_desc_t *d = stark_settings_desc(key);
    int v = (int)stark_settings_get(key) + dir * (int)d->step;
    if (v > (int)d->max) {
        v = wrap ? (int)d->min : (int)d->max;
    } else if (v < (int)d->min) {
        v = (int)d->min;
    }
    uint8_t old = stark_settings_get(key);
    return stark_settings_set(key, (uint8_t)v) == STARK_OK && old != (uint8_t)v;
}

static bool adjust(size_t index, int dir, void *ctx)
{
    (void)ctx;
    return step(index, dir, false);
}

static void restore_confirmed(stark_dialog_result_t result, void *ctx)
{
    (void)ctx;
    if (result == STARK_DIALOG_OK) {
        (void)stark_settings_restore_defaults();
        stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
    }
}

static void activate(size_t index, void *ctx)
{
    (void)ctx;
    if (index == ROW_RESTORE) {
        (void)stark_ui_dialog_confirm("Restore defaults?",
                                      "Every setting goes back to its default.", restore_confirmed,
                                      NULL);
    } else if (step(index, +1, true)) {
        stark_ui_invalidate(&s_menu.screen, stark_ui_content_rect());
    }
}

static stark_screen_t *settings_screen(void *state)
{
    (void)state;
    ui_menu_init(&s_menu, "Settings", k_items, sizeof k_items / sizeof k_items[0], activate, NULL);
    s_menu.value = value_of;
    s_menu.adjust = adjust;
    return &s_menu.screen;
}

/* BACK, long-BACK or a fault: what changed is written now. */
static void settings_stop(void *state)
{
    (void)state;
    (void)stark_settings_commit();
}

/* Launcher icon (16x16, 1bpp — ui_menu.h): three sliders. */
static const uint8_t k_icon[32] = {
    0x00, 0x00, 0x18, 0x00, 0xFF, 0xFF, 0x18, 0x00, 0x00, 0x00, 0x00, 0xC0, 0xFF, 0xFF, 0x00, 0xC0,
    0x00, 0x00, 0x00, 0x06, 0xFF, 0xFF, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

const stark_app_t app_settings = {
    .id = "settings",
    .title = "Settings",
    .category = "Device",
    .on_stop = settings_stop,
    .screen = settings_screen,
    .icon = k_icon,
};
