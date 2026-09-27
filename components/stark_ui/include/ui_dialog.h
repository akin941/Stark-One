/*
 * ui_dialog.h — the modal confirm / alert dialog (TASKS.md STARK-0105,
 * ARCHITECTURE.md §6.7). It is built to be the transmit-confirmation
 * primitive of the radio milestones (docs/SECURITY_SCOPE.md §4.2): the
 * default is cancel, and confirming always takes a deliberate, fresh OK.
 */
#pragma once

#include "stark_err.h"

typedef enum {
    STARK_DIALOG_CANCEL,
    STARK_DIALOG_OK,
} stark_dialog_result_t;

/* Runs after the dialog has been popped, so it may push a screen or open
 * another dialog. */
typedef void (*stark_dialog_done_fn)(stark_dialog_result_t result, void *ctx);

/*
 * Opens a confirm dialog over the current screen and returns at once. Title
 * (<= 31 bytes) and message (<= 159 bytes, wrapped to at most 4 lines) are
 * COPIED — truncated on a UTF-8 boundary — so the caller's buffers may go
 * away. Keys: OK (short) confirms, BACK (short) cancels, every other key is
 * consumed and ignored. An OK or BACK counts only if its PRESS arrived while
 * the dialog was open, so a key already held or queued can never confirm.
 * No timeout, no default-OK. Logs `dialog: open "<title>"` and then
 * `dialog: ok` or `dialog: cancel`; `done` (optional) gets the result. A
 * dialog removed any other way (STARK-0106's long-BACK home) reports
 * CANCEL. STARK_ERR_BUSY while a dialog is open, STARK_ERR_INVALID_ARG for
 * a NULL title, STARK_ERR_STATE before stark_ui_init().
 */
stark_err_t stark_ui_dialog_confirm(const char *title, const char *message,
                                    stark_dialog_done_fn done, void *ctx);

/* As confirm, but OK or BACK both close it with STARK_DIALOG_OK. */
stark_err_t stark_ui_dialog_alert(const char *title, const char *message, stark_dialog_done_fn done,
                                  void *ctx);
