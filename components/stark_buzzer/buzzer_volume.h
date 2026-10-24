/*
 * buzzer_volume.h — private to stark_buzzer: volume level -> PWM duty
 * (TASKS.md STARK-0204). Pure, host-tested. Loudness itself is a V1
 * measurement: a piezo's response to duty is not linear.
 */
#pragma once

#include <stdint.h>

#define BUZZER_VOLUME_MAX 3 /* 0 Off, 1 Low, 2 Medium, 3 High */

/* Duty in percent for a level; 0 (silent) for 0 and anything above the max. */
uint8_t buzzer_volume_duty(uint8_t level);
