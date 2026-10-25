/*
 * ui_port_nfc.c — host UI test port: settable stark_nfc backend
 * (ui_port_nfc.h). Writes complete at once.
 */
#include <string.h>
#include "stark_event.h"
#include "ui_port_nfc.h"

static stark_nfc_module_t s_module;
static stark_nfc_read_t s_tag;
static bool s_has_tag;
static bool s_polling;
static int s_writes;
static uint8_t s_msg[NFC_NDEF_MAX];
static size_t s_len;
static stark_err_t s_result, s_refuse, s_last;

static void publish(int32_t what)
{
    stark_event_t e = {.type = STARK_EVT_SYSTEM};
    e.system.id = STARK_SYS_NFC;
    e.system.arg = what;
    (void)stark_event_publish(&e);
}

void ui_port_nfc_reset(void)
{
    s_module = STARK_NFC_ABSENT;
    s_has_tag = s_polling = false;
    s_writes = 0;
    s_len = 0;
    s_result = s_refuse = s_last = STARK_OK;
}

void ui_port_nfc_set_module(stark_nfc_module_t m)
{
    s_module = m;
    publish(STARK_NFC_EVT_MODULE);
}

void ui_port_nfc_set_tag(const stark_nfc_read_t *r)
{
    s_has_tag = r != NULL;
    if (r != NULL) {
        s_tag = *r;
    }
    publish(r != NULL ? STARK_NFC_EVT_TAG : STARK_NFC_EVT_LEFT);
}

bool ui_port_nfc_polling(void)
{
    return s_polling;
}

int ui_port_nfc_writes(void)
{
    return s_writes;
}

const uint8_t *ui_port_nfc_last_write(size_t *len)
{
    *len = s_len;
    return s_msg;
}

void ui_port_nfc_write_outcome(stark_err_t result, stark_err_t refuse)
{
    s_result = result;
    s_refuse = refuse;
}

stark_err_t stark_nfc_init(void)
{
    return STARK_OK;
}

stark_nfc_module_t stark_nfc_module(void)
{
    return s_module;
}

stark_err_t stark_nfc_start(void)
{
    if (s_module == STARK_NFC_ABSENT || s_module == STARK_NFC_NOT_FITTED) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    s_polling = true;
    return STARK_OK;
}

stark_err_t stark_nfc_stop(void)
{
    s_polling = false;
    return STARK_OK;
}

stark_err_t stark_nfc_take(stark_nfc_read_t *out)
{
    if (!s_has_tag) {
        return STARK_ERR_NOT_FOUND;
    }
    *out = s_tag;
    return STARK_OK;
}

stark_err_t stark_nfc_write_ndef(const uint8_t *uid, uint8_t uid_len, const uint8_t *msg,
                                 size_t len)
{
    (void)uid;
    (void)uid_len;
    if (s_refuse != STARK_OK) {
        return s_refuse;
    }
    s_writes++;
    memcpy(s_msg, msg, len);
    s_len = len;
    s_last = s_result;
    publish(s_result == STARK_OK ? STARK_NFC_EVT_WRITE_DONE : STARK_NFC_EVT_WRITE_FAILED);
    return STARK_OK;
}

stark_err_t stark_nfc_write_result(void)
{
    return s_last;
}

/* A dump of the snapshot: pages filled with their own index. */
stark_err_t stark_nfc_request_dump(nfc_dump_t *out)
{
    if (!s_has_tag) {
        return STARK_ERR_STATE;
    }
    memset(out, 0, sizeof *out);
    out->tag = s_tag.tag;
    out->n_pages = s_tag.tag.pages;
    for (size_t i = 0; i < (size_t)out->n_pages * NFC_T2_PAGE_BYTES; i++) {
        out->mem[i] = (uint8_t)(i / NFC_T2_PAGE_BYTES);
    }
    publish(STARK_NFC_EVT_DUMP_DONE);
    return STARK_OK;
}

stark_err_t stark_nfc_dump_result(void)
{
    return STARK_OK;
}
