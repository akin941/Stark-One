/*
 * buzzer_volume.c — buzzer_volume.h. Pure. High (3) is the V0 duty, 50 %.
 */
#include "buzzer_volume.h"

uint8_t buzzer_volume_duty(uint8_t level)
{
    static const uint8_t k_duty[BUZZER_VOLUME_MAX + 1] = {0, 5, 15, 50};
    return level <= BUZZER_VOLUME_MAX ? k_duty[level] : 0;
}
