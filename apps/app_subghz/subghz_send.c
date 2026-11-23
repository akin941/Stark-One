/*
 * subghz_send.c — the Sub-GHz app's replay step (TASKS.md STARK-0605, ADR-0022 §6,
 * SECURITY_SCOPE §4): the capture the file view opened is checked against the transmit policy,
 * shown in a confirmation that says exactly what will be sent (the file, the frequency, the
 * duration, the symbols, the duty budget left) and sent once, only on OK. The service checks
 * again on the request and on the transmit path, and it only sends symbols that still hash to
 * what the confirmation showed. A refusal is explained on screen and logged by the service;
 * Cancel sends nothing. While a transmission runs every key is swallowed, and nothing here
 * repeats, queues or retries one.
 */
#include <stdio.h>
#include "stark_log.h"
#include "stark_subghz.h"
#include "subghz_rssi.h"
#include "subghzapp.h"
#include "ui_dialog.h"

static char s_name[16];         /* the file being sent, for the log */
static uint32_t s_hash;         /* what the confirmation showed */
static bool s_sending;          /* a request was accepted and its event has not come */
static const char *s_note = ""; /* the last outcome, for the view's Replay row */
static void (*s_refresh)(void); /* the app redraws the file view when the state changes */

void sgsend_bind(void (*refresh)(void))
{
    s_refresh = refresh;
}

static void changed(void)
{
    if (s_refresh != NULL) {
        s_refresh();
    }
}

bool sgsend_busy(void)
{
    return s_sending;
}

const char *sgsend_note(void)
{
    return s_note;
}

void sgsend_reset(void)
{
    s_sending = false;
    s_note = "";
}

void sgsend_refusal_text(subghz_tx_verdict_t v, uint32_t wait_ms, char *out, size_t size)
{
    switch (v) {
        case SUBGHZ_TX_REFUSE_BAND:
            (void)snprintf(out, size, "Outside 433.05-434.79 MHz.");
            break;
        case SUBGHZ_TX_REFUSE_LENGTH:
            (void)snprintf(out, size, "No symbols, or more than %u.",
                           (unsigned)SUBGHZ_TX_MAX_SYMBOLS);
            break;
        case SUBGHZ_TX_REFUSE_DURATION:
            (void)snprintf(out, size, "Longer than 2 s.");
            break;
        case SUBGHZ_TX_REFUSE_BUDGET:
            (void)snprintf(out, size, "Duty budget used up: 360 s in any hour.");
            break;
        case SUBGHZ_TX_REFUSE_OFFTIME:
            (void)snprintf(out, size, "Off-time: wait %lu s.",
                           (unsigned long)((wait_ms + 999u) / 1000u));
            break;
        case SUBGHZ_TX_REFUSE_FILE:
            (void)snprintf(out, size, "This file cannot be sent.");
            break;
        case SUBGHZ_TX_REFUSE_NO_MODULE:
            (void)snprintf(out, size, "The radio is not ready.");
            break;
        case SUBGHZ_TX_REFUSE_BUSY:
            (void)snprintf(out, size, "Already sending.");
            break;
        case SUBGHZ_TX_OK:
        case SUBGHZ_TX_VERDICT_COUNT:
        default:
            (void)snprintf(out, size, "Not sent.");
            break;
    }
}

static void explain(subghz_tx_verdict_t v, uint32_t wait_ms)
{
    char text[64];
    sgsend_refusal_text(v, wait_ms, text, sizeof text);
    s_note = "refused";
    changed(); /* the Replay row says so behind the alert */
    (void)stark_ui_dialog_alert("Not sent", text, NULL, NULL);
}

static void send_confirmed(stark_dialog_result_t result, void *ctx)
{
    (void)ctx;
    if (result != STARK_DIALOG_OK) {
        STARK_LOGI("subghzapp", "tx cancelled"); /* nothing was sent */
        return;
    }
    subghz_tx_info_t info;
    subghz_tx_verdict_t v = stark_subghz_tx_start(sgfile_loaded(), s_name, s_hash, &info);
    if (v == SUBGHZ_TX_OK) {
        s_sending = true; /* exactly one transmission is queued; its event ends the wait */
        s_note = "";
        STARK_LOGI("subghzapp", "tx confirmed %s", s_name);
        changed(); /* "sending...", the row dimmed */
    } else {
        explain(v, info.wait_ms); /* the service logged the reason */
    }
}

void sgsend_confirm(void)
{
    const subghz_raw_t *raw = sgfile_loaded();
    subghz_tx_info_t info;
    char mhz[12];
    char msg[112];
    if (s_sending) {
        return;
    }
    (void)snprintf(s_name, sizeof s_name, "%s", sgfile_loaded_name());
    s_note = "";
    subghz_tx_verdict_t v = stark_subghz_tx_check(raw, s_name, &info);
    if (v != SUBGHZ_TX_OK) {
        explain(v, info.wait_ms);
        return;
    }
    s_hash = info.hash;
    subghz_mhz_text(mhz, sizeof mhz, raw->freq_hz);
    char title[24];
    (void)snprintf(title, sizeof title, "Send %s?", s_name);
    (void)snprintf(msg, sizeof msg,
                   "%s MHz, %lu ms, %u symbols. Duty left %lu s. Own devices only.", mhz,
                   (unsigned long)info.duration_ms, (unsigned)raw->n,
                   (unsigned long)(info.budget_left_ms / 1000u));
    STARK_LOGI("subghzapp", "confirm %s: %s", s_name, msg);
    (void)stark_ui_dialog_confirm(title, msg, send_confirmed, NULL);
}

bool sgsend_event(const stark_event_t *e)
{
    if (e->type != STARK_EVT_SYSTEM || e->system.id != STARK_SYS_SUBGHZ || !s_sending) {
        return false;
    }
    switch (e->system.arg) {
        case STARK_SUBGHZ_EVT_TX_DONE:
            STARK_LOGI("subghzapp", "sent %s", s_name);
            s_note = "sent";
            break;
        case STARK_SUBGHZ_EVT_TX_FAILED:
            STARK_LOGW("subghzapp", "send failed: %s", s_name);
            s_note = "failed";
            break;
        case STARK_SUBGHZ_EVT_TX_REFUSED:
            STARK_LOGW("subghzapp", "send refused: %s", s_name);
            s_note = "refused";
            break;
        default:
            return false;
    }
    s_sending = false;
    return true;
}
