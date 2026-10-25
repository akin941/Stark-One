/*
 * nfc_write.c — the NFC app's write (TASKS.md STARK-0404, ADR-0020 §5): one
 * of the project's own lab payloads, written to the present tag only after a
 * confirmation that names the tag and the content. The device has no text
 * entry, so the payloads are fixed.
 */
#include <stdio.h>
#include "ndef.h"
#include "nfcapp.h"
#include "stark_log.h"
#include "ui_dialog.h"

typedef struct {
    const char *label;
    const char *lang; /* NULL: a URI record */
    const char *text;
} payload_t;

static const payload_t k_payloads[] = {
    {"https://example.com/stark", NULL, "https://example.com/stark"},
    {"https://example.org/lab", NULL, "https://example.org/lab"},
    {"Text: STARK ONE lab", "en", "STARK ONE lab"},
};
#define N_PAYLOADS (sizeof k_payloads / sizeof k_payloads[0])

static ui_menu_item_t s_items[N_PAYLOADS];
static uint8_t s_msg[96];
static size_t s_len;
static nfc_tag_t s_tag;
static char s_uid[2 * NFC_UID_MAX + 1];

const ui_menu_item_t *nfcwrite_items(size_t *count)
{
    for (size_t i = 0; i < N_PAYLOADS; i++) {
        s_items[i] = (ui_menu_item_t){.label = k_payloads[i].label};
    }
    *count = N_PAYLOADS;
    return s_items;
}

static void confirmed(stark_dialog_result_t result, void *ctx)
{
    (void)ctx;
    if (result != STARK_DIALOG_OK) {
        STARK_LOGI("nfcapp", "write cancelled");
        return;
    }
    stark_err_t err = stark_nfc_write_ndef(s_tag.uid, s_tag.uid_len, s_msg, s_len);
    if (err != STARK_OK) {
        STARK_LOGW("nfcapp", "write refused: %s", stark_err_str(err));
    }
}

void nfcwrite_activate(size_t index)
{
    const char *why = NULL;
    const stark_nfc_read_t *r = nfcview_writable(&why);
    if (index >= N_PAYLOADS || r == NULL) {
        STARK_LOGI("nfcapp", "write refused: %s", why != NULL ? why : "no payload");
        return;
    }
    const payload_t *p = &k_payloads[index];
    uint8_t payload[64];
    ndef_record_t rec;
    stark_err_t err = p->lang == NULL
                          ? ndef_uri_make(p->text, payload, sizeof payload, &rec)
                          : ndef_text_make(p->lang, p->text, payload, sizeof payload, &rec);
    s_len = err == STARK_OK ? ndef_encode(&rec, 1, s_msg, sizeof s_msg) : 0;
    if (s_len == 0 || s_len > r->ndef_capacity) {
        STARK_LOGI("nfcapp", "write refused: too large");
        return;
    }
    s_tag = r->tag;
    for (unsigned i = 0; i < s_tag.uid_len; i++) {
        snprintf(s_uid + 2 * i, 3, "%02X", s_tag.uid[i]);
    }
    s_uid[2 * s_tag.uid_len] = '\0';
    char what[48];
    char msg[128];
    ndef_describe(&rec, what, sizeof what);
    /* SECURITY_SCOPE §2: say exactly what will be written, and to which tag. */
    snprintf(msg, sizeof msg, "%s: %s (%u B of %u). Own tags only.", s_uid, what, (unsigned)s_len,
             (unsigned)r->ndef_capacity);
    STARK_LOGI("nfcapp", "confirm %s", msg);
    (void)stark_ui_dialog_confirm("Write NFC tag?", msg, confirmed, NULL);
}

void nfcwrite_event(const stark_event_t *e)
{
    if (e->type != STARK_EVT_SYSTEM || e->system.id != STARK_SYS_NFC) {
        return;
    }
    if (e->system.arg == STARK_NFC_EVT_WRITE_DONE) {
        STARK_LOGI("nfcapp", "wrote %s", s_uid);
    } else if (e->system.arg == STARK_NFC_EVT_WRITE_FAILED) {
        STARK_LOGW("nfcapp", "write failed: %s", stark_err_str(stark_nfc_write_result()));
    }
}
