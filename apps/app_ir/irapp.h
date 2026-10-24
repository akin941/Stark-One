/*
 * irapp.h — private to the IR app (TASKS.md STARK-0305/0306).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "ir_raw.h"
#include "ir_types.h"
#include "stark_event.h"
#include "ui_menu.h"

#define IRAPP_RECENT     6  /* captures shown, newest first */
#define IRAPP_BROWSE_MAX 16 /* saved files listed */

/* The worker's buffers, shared by save and browse (ir_save.c).
 * NOTE: stark_app runs one worker at a time, and the IR app starts one only
 * from the screen that owns the result (Save from the receive view, list and
 * load from Browse), so the two never hold these at once. Sharing them keeps
 * ~2.3 kB of static RAM out of the app (measured at STARK-0307). */
typedef struct {
    ir_signal_t signal;         /* save: the capture; browse: the file loaded */
    char text[IR_RAW_TEXT_MAX]; /* the `.ir` text written or read */
} irapp_work_t;
extern irapp_work_t irapp_work;

/* Saves s as the next captures/ir_NNNN.ir on the app worker (ir_save.c).
 * Logs `irapp: saved <name>`, `irapp: save failed: <err>` or
 * `irapp: save refused: <why>`. */
void irapp_save(const ir_signal_t *s);

/* On the worker: reads and parses captures/<name> (ir_save.c). */
stark_err_t irapp_load(const char *name, ir_signal_t *out);

/* "NEC 00/12", "RC5 05/35", "NEC repeat", "RAW 67" (ir_view.c). */
void irapp_describe(const ir_signal_t *s, char *out, size_t len);

/* ---- Receive view (ir_view.c, STARK-0305) ---- */
void irview_reset(void);                           /* app start: no captures */
const ui_menu_item_t *irview_items(size_t *count); /* rows, rebuilt */
const char *irview_value(size_t index);            /* a capture's "38k n67" */
void irview_activate(size_t index);                /* save / browse */
void irview_drain(void);                           /* take new captures */

/* Switch the app's one screen to the browse list / back (app_ir.c). */
void irapp_show_browse(void);

/* ---- Browse mode (ir_browse.c, STARK-0306) ---- */
typedef struct {
    char name[24];
    char what[24]; /* irapp_describe() of the parsed file, or "(invalid)" */
    bool valid;
} irbrowse_entry_t;

/* Lists captures/ *.ir on the worker; irapp_show_browse() follows. */
void irbrowse_open(void);
const irbrowse_entry_t *irbrowse_entries(size_t *count);
/* A list or load is running on the worker (it owns irapp_work). */
bool irbrowse_busy(void);
/* The app stopped (its worker joined): forget the list and any pending
 * list/load/send. */
void irbrowse_stop(void);
/* The card went away: the list empties (no file can be read any more). */
void irbrowse_forget(void);
/* OK on an entry: load it, confirm, transmit. */
void irbrowse_activate(size_t index);
/* Worker notifications; true when consumed. */
bool irbrowse_event(const stark_event_t *e);

/* ---- Transmit (ir_send.c, STARK-0306) ---- */
/* Gate-checks irapp_work.signal, logs and shows the confirmation; OK sends. */
void irsend_confirm(const char *name);
void irsend_event(const stark_event_t *e); /* logs `irapp: sent <name>` */
void irsend_stop(void);
