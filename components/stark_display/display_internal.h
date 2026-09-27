/*
 * display_internal.h — private to stark_display: state shared by the panel
 * bring-up (display_panel.c) and the band renderer (display_render.c).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#define DISPLAY_W           320
#define DISPLAY_H           240
#define DISPLAY_BAND_H      CONFIG_STARK_DISPLAY_BAND_H
#define DISPLAY_BAND_PIXELS ((size_t)DISPLAY_W * DISPLAY_BAND_H)
#define DISPLAY_NUM_BUFS    2
#define DISPLAY_NUM_BANDS   ((DISPLAY_H + DISPLAY_BAND_H - 1) / DISPLAY_BAND_H)

typedef struct {
    esp_lcd_panel_handle_t panel;
    SemaphoreHandle_t flush_done;    /* counting: one give per finished transfer */
    uint16_t *buf[DISPLAY_NUM_BUFS]; /* DMA-capable, DISPLAY_BAND_PIXELS each */
    bool ready;
} display_state_t;

extern display_state_t display_state;
