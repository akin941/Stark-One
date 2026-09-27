/*
 * app_about.c — About (TASKS.md STARK-0020): firmware, IDF, chip, flash,
 * board, free heap, uptime. The last two refresh once per second and only
 * their rows are redrawn (partial redraws).
 */
#include <inttypes.h>
#include <stdio.h>
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_timer.h"
#include "gfx_font.h"
#include "stark_app.h"
#include "stark_board.h"
#include "stark_log.h"
#include "ui_format.h"

#define ROW_Y(i) (int16_t)(16 + 8 + (i) * 22)
#define ROW_H    22
#define ROW_HEAP 5
#define ROW_UP   6
#define N_ROWS   7

static char s_rows[N_ROWS][48]; /* "Firmware  " + a 31-char app version fits */
static esp_timer_handle_t s_timer;
static stark_screen_t s_screen;

static void on_second(void *arg)
{
    (void)arg;
    const stark_event_t tick = {.type = STARK_EVT_TICK};
    (void)stark_event_publish(&tick); /* the UI task refreshes on it */
}

static void refresh_dynamic(void)
{
    char up[24];
    unsigned heap = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    (void)ui_format_uptime((uint64_t)(esp_timer_get_time() / 1000000), up, sizeof up);
    snprintf(s_rows[ROW_HEAP], sizeof s_rows[0], "Free heap %u B", heap);
    snprintf(s_rows[ROW_UP], sizeof s_rows[0], "Uptime    %s", up);
    STARK_LOGI("about", "heap=%u uptime=%s", heap, up);
}

static bool about_event(stark_screen_t *self, const stark_event_t *e)
{
    if (e->type != STARK_EVT_TICK) {
        return false; /* BACK and the rest belong to stark_ui */
    }
    refresh_dynamic();
    stark_ui_invalidate(self, (gfx_rect_t){0, ROW_Y(ROW_HEAP), 320, 2 * ROW_H});
    return true;
}

static void about_render(stark_screen_t *self, gfx_surface_t *s)
{
    (void)self;
    gfx_fill(s, s->clip, GFX_RGB565(0, 0, 0));
    for (int i = 0; i < N_ROWS; i++) {
        (void)gfx_text(s, &gfx_font_mono16, 8, ROW_Y(i), s_rows[i], GFX_RGB565(255, 255, 255),
                       GFX_RGB565(0, 0, 0), true);
    }
}

static stark_err_t about_start(void **state)
{
    (void)state;
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash = 0;
    (void)esp_flash_get_size(NULL, &flash);
    snprintf(s_rows[0], sizeof s_rows[0], "Firmware  %s", esp_app_get_description()->version);
    snprintf(s_rows[1], sizeof s_rows[0], "ESP-IDF   %s", esp_get_idf_version());
    snprintf(s_rows[2], sizeof s_rows[0], "Chip      %s rev %u.%u", CONFIG_IDF_TARGET,
             (unsigned)(chip.revision / 100), (unsigned)(chip.revision % 100));
    snprintf(s_rows[3], sizeof s_rows[0], "Flash     %" PRIu32 " MB", flash / (1024u * 1024u));
    snprintf(s_rows[4], sizeof s_rows[0], "Board     %s", stark_board_name());
    STARK_LOGI("about", "board=%s flash=%" PRIu32 "MB chip=%s", stark_board_name(),
               flash / (1024u * 1024u), CONFIG_IDF_TARGET);
    refresh_dynamic();

    const esp_timer_create_args_t args = {.callback = on_second, .name = "about"};
    STARK_CHECK_RET(esp_timer_create(&args, &s_timer) == ESP_OK ? STARK_OK : STARK_ERR_NO_MEM);
    if (esp_timer_start_periodic(s_timer, 1000000) != ESP_OK) {
        (void)esp_timer_delete(s_timer);
        return STARK_ERR_IO;
    }
    return STARK_OK;
}

static void about_stop(void *state)
{
    (void)state;
    (void)esp_timer_stop(s_timer);
    (void)esp_timer_delete(s_timer);
    s_timer = NULL;
}

static stark_screen_t *about_screen(void *state)
{
    (void)state;
    s_screen =
        (stark_screen_t){.name = "About", .on_event = about_event, .on_render = about_render};
    return &s_screen;
}

const stark_app_t app_about = {
    .id = "about",
    .title = "About",
    .category = "System",
    .on_start = about_start,
    .on_stop = about_stop,
    .screen = about_screen,
};
