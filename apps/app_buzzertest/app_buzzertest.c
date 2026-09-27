/*
 * app_buzzertest.c — Buzzer Test (TASKS.md STARK-0020): a few fixed tones
 * and a short melody in a list; OK plays, BACK exits. The screen is a
 * ui_menu, so navigation behaves exactly like the launcher.
 */
#include "stark_app.h"
#include "stark_buzzer.h"
#include "stark_log.h"
#include "ui_menu.h"

static const ui_menu_item_t k_items[] = {
    {"440 Hz  (A4)", false}, {"1 kHz", false},  {"2 kHz", false},
    {"4 kHz", false},        {"Melody", false},
};
static const uint16_t k_tones[] = {440, 1000, 2000, 4000};
static const stark_buzzer_note_t k_melody[] = {
    {523, 150}, {659, 150}, {784, 150}, {0, 60}, {1047, 300},
};

static ui_menu_t s_menu;

static void play(size_t index, void *ctx)
{
    (void)ctx;
    if (index < sizeof k_tones / sizeof k_tones[0]) {
        (void)stark_buzzer_tone(k_tones[index], 400);
        STARK_LOGI("buzzertest", "tone %u Hz", (unsigned)k_tones[index]);
    } else {
        (void)stark_buzzer_play(k_melody, sizeof k_melody / sizeof k_melody[0]);
        STARK_LOGI("buzzertest", "melody");
    }
}

static void buzzer_stop(void *state)
{
    (void)state;
    stark_buzzer_stop();
}

static stark_screen_t *buzzer_screen(void *state)
{
    (void)state;
    ui_menu_init(&s_menu, "Buzzer Test", k_items, sizeof k_items / sizeof k_items[0], play, NULL);
    return &s_menu.screen;
}

const stark_app_t app_buzzertest = {
    .id = "buzzertest",
    .title = "Buzzer Test",
    .category = "System",
    .on_stop = buzzer_stop,
    .screen = buzzer_screen,
};
