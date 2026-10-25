/*
 * nfcapp.h — private to the NFC app (TASKS.md STARK-0404/0405).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "stark_event.h"
#include "stark_nfc.h"
#include "ui_menu.h"

/* ---- Tag view (nfc_view.c) ---- */
void nfcview_refresh(void);                         /* take the snapshot again */
void nfcview_log_records(void);                     /* `nfcapp: record <line>` each */
const ui_menu_item_t *nfcview_items(size_t *count); /* rows for the module/tag state */
const char *nfcview_value(size_t index);
bool nfcview_is_write_row(size_t index);
bool nfcview_is_save_row(size_t index);
/* The present tag when it can be written; otherwise NULL and *why. */
const stark_nfc_read_t *nfcview_writable(const char **why);

/* ---- Write (nfc_write.c) ---- */
const ui_menu_item_t *nfcwrite_items(size_t *count); /* the lab payloads */
void nfcwrite_activate(size_t index);                /* confirm, then write */
void nfcwrite_event(const stark_event_t *e);         /* logs a write's result */

/* ---- Dump to the card (nfc_save.c, STARK-0405) ---- */
void nfcsave_request(void);                 /* dump the present tag, then save it */
void nfcsave_event(const stark_event_t *e); /* DUMP_DONE: write it on the worker */
void nfcsave_stop(void);                    /* the app stopped */

/* Switch the app's one screen to the payload list / back (app_nfc.c). */
void nfcapp_show_write(void);
void nfcapp_show_view(void);
