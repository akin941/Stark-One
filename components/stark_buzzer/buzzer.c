/*
 * buzzer.c — tone sequencer on LEDC channel 0 via stark_hal PWM, stepped by
 * a one-shot esp_timer (TASKS.md STARK-0020). Target-only.
 *
 * The caller's task and the esp_timer task share the sequence; one mutex
 * covers reading it, driving the PWM and arming the timer, so they never
 * interleave. Each arming records when it is due; a callback that runs
 * well before the current deadline fired for an older arming (it was
 * already queued when a newer request re-armed the timer) and steps aside.
 * esp_timer never fires early and a note lasts >= 1 ms, so the check is
 * exact. (esp_timer_is_active() cannot be used for this: inside a one-shot
 * timer's own callback it does not report the timer idle.)
 */
#include "stark_buzzer.h"
#include <stdbool.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "stark_board.h"
#include "stark_hal.h"
#include "stark_hal_esp.h"

#define BUZZER_CHANNEL 0 /* LEDC allocation: stark_hal.h */
#define BUZZER_DUTY    50
#define BUZZER_MIN_HZ  200
#define BUZZER_MAX_HZ  10000
#define BUZZER_MAX_MS  10000

#define CLICK_HZ       2000
#define CLICK_MS       20
#define REJECT_HZ      300
#define REJECT_MS      60

static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_timer;
static bool s_ready;
static bool s_has_pin;
static stark_buzzer_note_t s_seq[STARK_BUZZER_MAX_NOTES];
static size_t s_len;
static size_t s_next;         /* index of the note to start next */
static int64_t s_deadline_us; /* when the current arming is due */

static void sound(uint16_t hz)
{
    if (!s_has_pin) {
        return;
    }
    if (hz == 0) {
        (void)stark_hal_pwm_set_duty_pct(BUZZER_CHANNEL, 0);
        return;
    }
    (void)stark_hal_pwm_set_freq(BUZZER_CHANNEL, hz);
    (void)stark_hal_pwm_set_duty_pct(BUZZER_CHANNEL, BUZZER_DUTY);
}

/* Caller holds s_lock: starts note s_next (or goes silent past the end)
 * and arms the timer for its length. */
static void step_locked(void)
{
    if (s_next >= s_len) {
        sound(0);
        return;
    }
    stark_buzzer_note_t note = s_seq[s_next++];
    sound(note.hz);
    s_deadline_us = esp_timer_get_time() + (int64_t)note.ms * 1000;
    (void)esp_timer_start_once(s_timer, (uint64_t)note.ms * 1000u);
}

static void on_timer(void *arg)
{
    (void)arg;
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    if (esp_timer_get_time() + 200 >= s_deadline_us) { /* else: stale, see top */
        step_locked();
    }
    (void)xSemaphoreGive(s_lock);
}

static bool note_ok(const stark_buzzer_note_t *n)
{
    return n->ms >= 1 && n->ms <= BUZZER_MAX_MS &&
           (n->hz == 0 || (n->hz >= BUZZER_MIN_HZ && n->hz <= BUZZER_MAX_HZ));
}

stark_err_t stark_buzzer_init(void)
{
    if (s_ready) {
        return STARK_ERR_STATE;
    }
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    int pin = stark_board_pins()->buzzer;
    s_has_pin = pin >= 0;
    if (s_has_pin) {
        STARK_CHECK_RET(stark_hal_pwm_init(pin, CLICK_HZ, BUZZER_CHANNEL));
        STARK_CHECK_RET(stark_hal_pwm_set_duty_pct(BUZZER_CHANNEL, 0));
    }
    const esp_timer_create_args_t args = {
        .callback = on_timer,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "stark_buzzer",
    };
    STARK_CHECK_RET(stark_err_from_esp(esp_timer_create(&args, &s_timer)));
    s_ready = true;
    return STARK_OK;
}

stark_err_t stark_buzzer_play(const stark_buzzer_note_t *notes, size_t count)
{
    if (!s_ready) {
        return STARK_ERR_STATE;
    }
    if (notes == NULL || count == 0 || count > STARK_BUZZER_MAX_NOTES) {
        return STARK_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < count; i++) {
        if (!note_ok(&notes[i])) {
            return STARK_ERR_INVALID_ARG;
        }
    }
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    (void)esp_timer_stop(s_timer); /* not running is fine */
    for (size_t i = 0; i < count; i++) {
        s_seq[i] = notes[i];
    }
    s_len = count;
    s_next = 0;
    step_locked();
    (void)xSemaphoreGive(s_lock);
    return STARK_OK;
}

stark_err_t stark_buzzer_tone(uint16_t hz, uint16_t ms)
{
    const stark_buzzer_note_t note = {hz, ms};
    if (hz == 0) {
        return STARK_ERR_INVALID_ARG; /* a tone, not a rest */
    }
    return stark_buzzer_play(&note, 1);
}

void stark_buzzer_stop(void)
{
    if (!s_ready) {
        return;
    }
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    (void)esp_timer_stop(s_timer);
    s_len = 0;
    s_next = 0;
    sound(0);
    (void)xSemaphoreGive(s_lock);
}

void stark_buzzer_click(void)
{
    (void)stark_buzzer_tone(CLICK_HZ, CLICK_MS);
}

void stark_buzzer_reject(void)
{
    (void)stark_buzzer_tone(REJECT_HZ, REJECT_MS);
}
