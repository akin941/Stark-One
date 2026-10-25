/*
 * nfc_poll.c — the poll state machine (nfc_poll.h). Pure.
 */
#include "nfc_poll.h"
#include <string.h>

void nfc_poll_init(nfc_poll_t *p)
{
    memset(p, 0, sizeof *p);
}

nfc_poll_event_t nfc_poll_result(nfc_poll_t *p, stark_err_t err, bool found, const nfc_tag_t *tag,
                                 uint32_t now_ms)
{
    if (p->state == NFC_POLL_RECOVERING) {
        return NFC_POLL_NONE;
    }
    if (err != STARK_OK) {
        if (++p->errors >= NFC_POLL_ERRORS) {
            nfc_poll_init(p);
            p->state = NFC_POLL_RECOVERING;
            p->recover_at = now_ms + NFC_POLL_RECOVER_MS;
            return NFC_POLL_LOST;
        }
        return NFC_POLL_NONE;
    }
    p->errors = 0;
    if (found) {
        p->misses = 0;
        if (p->state == NFC_POLL_PRESENT && p->uid_len == tag->uid_len &&
            memcmp(p->uid, tag->uid, tag->uid_len) == 0) {
            return NFC_POLL_NONE;
        }
        p->state = NFC_POLL_PRESENT;
        p->uid_len = tag->uid_len;
        memcpy(p->uid, tag->uid, tag->uid_len);
        return NFC_POLL_TAG;
    }
    if (p->state == NFC_POLL_PRESENT && ++p->misses >= NFC_POLL_MISSES) {
        nfc_poll_init(p);
        return NFC_POLL_LEFT;
    }
    return NFC_POLL_NONE;
}

bool nfc_poll_recover_due(const nfc_poll_t *p, uint32_t now_ms)
{
    return p->state == NFC_POLL_RECOVERING && (int32_t)(now_ms - p->recover_at) >= 0;
}

nfc_poll_event_t nfc_poll_recovered(nfc_poll_t *p, stark_err_t err, uint32_t now_ms)
{
    if (p->state != NFC_POLL_RECOVERING) {
        return NFC_POLL_NONE;
    }
    if (err != STARK_OK) {
        p->recover_at = now_ms + NFC_POLL_RECOVER_MS;
        return NFC_POLL_NONE;
    }
    nfc_poll_init(p);
    return NFC_POLL_BACK;
}
