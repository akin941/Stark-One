/*
 * subghzapp.h — private to the Sub-GHz app (TASKS.md STARK-0602/0603): the views and the
 * text they show, built from stark_subghz's status, and the card I/O of captures
 * (subghz_files.c), which runs on the app worker and never on the UI task.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_event.h"
#include "stark_subghz.h"
#include "subghz_raw.h"
#include "ui_menu.h"

typedef enum {
    SGAPP_HOME,    /* the radio is idle: pick a view */
    SGAPP_METER,   /* live RSSI at one frequency */
    SGAPP_SCAN,    /* one sweep of the band */
    SGAPP_CAPTURE, /* setup, capturing, or the finished capture (by the service's status) */
    SGAPP_SAVED,   /* the saved captures */
    SGAPP_FILE     /* one saved capture, read back */
} sgapp_mode_t;

#define SGAPP_DEFAULT_HZ 433920000u
#define SGAPP_LIST_MAX   16 /* saved captures listed, newest first */

/* Takes the module state and the latest measurement again. */
void sgview_refresh(void);
/* Forgets the session: the default frequency and modulation, and "was never ready". */
void sgview_reset(void);

/* The meter's and the capture's frequency; sgview_step_hz() moves it, clamped to the band. */
uint32_t sgview_hz(void);
bool sgview_step_hz(int dir); /* false when it did not move (an edge of the band) */

/* The rows for a view: a single notice row while there is no module. */
const ui_menu_item_t *sgview_items(sgapp_mode_t mode, size_t *count);
const char *sgview_value(size_t index);

/* What an activated row means. */
bool sgview_is_meter_row(size_t index);   /* HOME: open the meter */
bool sgview_is_scan_row(size_t index);    /* HOME: open the scan */
bool sgview_is_capture_row(size_t index); /* HOME: open the capture view */
bool sgview_is_saved_row(size_t index);   /* HOME: open the saved captures */
bool sgview_is_again_row(size_t index);   /* SCAN: sweep again */
bool sgview_ready(void);

/* ---- capture view (subghz_view.c) ---- */
typedef enum {
    SGCAP_SETUP,   /* frequency and modulation, then Start */
    SGCAP_RUNNING, /* symbols arriving; Stop */
    SGCAP_DONE     /* the histogram; Save and Capture again */
} sgcap_phase_t;

sgcap_phase_t sgview_cap_phase(void);
subghz_mod_t sgview_mod(void);
void sgview_toggle_mod(void);
bool sgview_is_start_row(size_t index);   /* SETUP: begin */
bool sgview_is_stop_row(size_t index);    /* RUNNING: end it now */
bool sgview_is_save_row(size_t index);    /* DONE: save it */
bool sgview_is_restart_row(size_t index); /* DONE: capture again */
/* The capture as a raw description, from the status and the chosen modulation. */
bool sgview_capture_raw(subghz_raw_t *out, subghz_sym_t *symbols);

/* ---- saved captures (subghz_files.c) ---- */
typedef struct {
    char name[16];
    uint32_t size;
} sgfile_entry_t;

typedef enum {
    SGFILE_NOTE_NONE,
    SGFILE_NOTE_SAVED, /* sgfile_saved_name() is the file */
    SGFILE_NOTE_SAVE_FAILED,
    SGFILE_NOTE_LISTED,
    SGFILE_NOTE_OPENED, /* sgfile_loaded() is the capture */
    SGFILE_NOTE_OPEN_FAILED
} sgfile_note_t;

/*
 * The one symbol buffer (2048 x 4 B), taken from the heap when a capture starts or a file
 * is opened and given back when the app stops: nothing is held while the app is closed.
 * NULL when memory is short.
 */
subghz_sym_t *sgfile_buffer(void);
subghz_sym_t *sgfile_symbols(void); /* the buffer if it is held, else NULL */
void sgfile_stop(void); /* the app stopped (its worker is joined): free the buffer, forget all */
bool sgfile_busy(void); /* a worker owns the buffer and the list */

/* Writes the capture (valid raw over the buffer) as the next captures/rf_NNNN.rf. */
void sgfile_save(const subghz_raw_t *raw);
const char *sgfile_saved_name(void); /* "" until the current capture is saved */
void sgfile_forget_save(void);       /* a new capture starts */

void sgfile_list(void); /* lists on the worker; SGFILE_NOTE_LISTED follows */
const sgfile_entry_t *sgfile_entries(size_t *count);
void sgfile_open(size_t index); /* reads and parses on the worker; _OPENED or _OPEN_FAILED */
const subghz_raw_t *sgfile_loaded(void);
const char *sgfile_loaded_name(void);
/* The card went away: the list and the open file are forgotten. */
void sgfile_forget(void);

/* Worker notifications; true when consumed, *what says which. */
bool sgfile_event(const stark_event_t *e, sgfile_note_t *what);

/* "400x92 1200x96" (mean µs x count, ascending, then "other=n"); for logs. */
void sgview_hist_text(const subghz_hist_t *h, char *out, size_t size);

/* ---- file view rows (subghz_view.c) ---- */
void sgview_set_file(const char *name, const subghz_raw_t *raw);
