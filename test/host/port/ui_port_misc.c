/*
 * ui_port_misc.c — host UI test port: stark_buzzer.h recorder, key-event
 * helpers and the record reset.
 */
#include "stark_buzzer.h"
#include "stark_event.h"
#include "stark_hal.h"
#include "ui_port.h"

void ui_port_display_reset(void);
void ui_port_log_clear(void);

static int s_clicks;
static int s_rejects;

stark_err_t stark_buzzer_init(void)
{
    return STARK_OK;
}

stark_err_t stark_buzzer_tone(uint16_t hz, uint16_t ms)
{
    (void)hz;
    (void)ms;
    return STARK_OK;
}

stark_err_t stark_buzzer_play(const stark_buzzer_note_t *notes, size_t count)
{
    (void)notes;
    (void)count;
    return STARK_OK;
}

void stark_buzzer_stop(void)
{}

void stark_buzzer_click(void)
{
    s_clicks++;
}

void stark_buzzer_reject(void)
{
    s_rejects++;
}

int ui_port_buzzer_clicks(void)
{
    return s_clicks;
}

int ui_port_buzzer_rejects(void)
{
    return s_rejects;
}

void ui_port_reset_records(void)
{
    ui_port_display_reset();
    ui_port_log_clear();
    s_clicks = 0;
    s_rejects = 0;
}

void ui_port_key(stark_key_t key, stark_key_action_t action)
{
    stark_event_t e = {.type = STARK_EVT_KEY, .ts_us = stark_hal_now_us()};
    e.key.key = (uint8_t)key;
    e.key.action = (uint8_t)action;
    (void)stark_event_publish(&e);
}

void ui_port_short(stark_key_t key)
{
    ui_port_key(key, STARK_KEY_PRESS);
    ui_port_key(key, STARK_KEY_RELEASE);
    ui_port_key(key, STARK_KEY_SHORT);
}
