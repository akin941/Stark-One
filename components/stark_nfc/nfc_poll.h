/*
 * nfc_poll.h — private to stark_nfc: the poll state machine (TASKS.md
 * STARK-0403). Pure; the NFC task feeds it each poll's outcome and acts on
 * the event it returns.
 *
 * A new UID is TAG (read it); the same UID while present is nothing; two
 * empty polls in a row are LEFT, so one missed poll does not lose a tag;
 * NFC_POLL_ERRORS transport errors in a row are LOST, after which the task
 * re-detects the module every NFC_POLL_RECOVER_MS until BACK.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "nfc_types.h"
#include "stark_err.h"

#define NFC_POLL_MISSES     2u
#define NFC_POLL_ERRORS     3u
#define NFC_POLL_RECOVER_MS 1000u

typedef enum { NFC_POLL_IDLE, NFC_POLL_PRESENT, NFC_POLL_RECOVERING } nfc_poll_state_t;
typedef enum {
    NFC_POLL_NONE,
    NFC_POLL_TAG,  /* a tag arrived, or another one replaced it */
    NFC_POLL_LEFT, /* the tag is gone */
    NFC_POLL_LOST, /* the module stopped answering */
    NFC_POLL_BACK  /* it answers again */
} nfc_poll_event_t;

typedef struct {
    nfc_poll_state_t state;
    uint8_t uid[NFC_UID_MAX];
    uint8_t uid_len;
    uint8_t misses;
    uint8_t errors;
    uint32_t recover_at;
} nfc_poll_t;

void nfc_poll_init(nfc_poll_t *p);

/* One poll: err from the transport/driver, and when it is STARK_OK whether a
 * tag answered and which. */
nfc_poll_event_t nfc_poll_result(nfc_poll_t *p, stark_err_t err, bool found, const nfc_tag_t *tag,
                                 uint32_t now_ms);

/* RECOVERING and due for another detection attempt. */
bool nfc_poll_recover_due(const nfc_poll_t *p, uint32_t now_ms);

/* The outcome of that attempt. */
nfc_poll_event_t nfc_poll_recovered(nfc_poll_t *p, stark_err_t err, uint32_t now_ms);
