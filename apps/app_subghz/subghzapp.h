/*
 * subghzapp.h — private to the Sub-GHz app (TASKS.md STARK-0602): the three views
 * and the text they show, built from stark_subghz's status.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_subghz.h"
#include "ui_menu.h"

typedef enum {
    SGAPP_HOME,  /* the radio is idle: pick a view */
    SGAPP_METER, /* live RSSI at one frequency */
    SGAPP_SCAN   /* one sweep of the band */
} sgapp_mode_t;

#define SGAPP_DEFAULT_HZ 433920000u

/* Takes the module state and the latest measurement again. */
void sgview_refresh(void);
/* Forgets the session: the default frequency and "was never ready". */
void sgview_reset(void);

/* The meter's frequency; sgview_step_hz() moves it by dir steps, clamped to the band. */
uint32_t sgview_hz(void);
bool sgview_step_hz(int dir); /* false when it did not move (an edge of the band) */

/* The rows for a view: a single notice row while there is no module. */
const ui_menu_item_t *sgview_items(sgapp_mode_t mode, size_t *count);
const char *sgview_value(size_t index);

/* What an activated row means. */
bool sgview_is_meter_row(size_t index); /* HOME: open the meter */
bool sgview_is_scan_row(size_t index);  /* HOME: open the scan */
bool sgview_is_again_row(size_t index); /* SCAN: sweep again */
bool sgview_ready(void);
