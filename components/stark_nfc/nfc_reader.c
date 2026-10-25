/*
 * nfc_reader.c — finding and reading a tag (nfc_reader.h). Pure.
 */
#include "nfc_reader.h"
#include <string.h>
#include "nfc_ident.h"
#include "nfc_type2.h"

#define STATUS_TIMEOUT 0x01u

stark_err_t nfc_reader_poll(pn532_t *d, bool *found, nfc_tag_t *tag)
{
    static const uint8_t p[] = {0x01, 0x00}; /* one target, 106 kbit/s type A */
    uint8_t r[64];
    size_t got = 0;
    *found = false;
    STARK_CHECK_RET(pn532_command(d, PN532_CMD_LIST_PASSIVE, p, sizeof p, r, sizeof r, &got,
                                  NFC_EXCHANGE_TIMEOUT_MS));
    if (got < 1 || r[0] == 0) {
        return STARK_OK;
    }
    /* NbTg, Tg, SENS_RES (2), SEL_RES, NFCIDLength, NFCID1 */
    if (got < 6 || (r[5] != 4 && r[5] != 7 && r[5] != 10) || got < 6u + r[5]) {
        return STARK_ERR_IO;
    }
    memset(tag, 0, sizeof *tag);
    tag->atqa = (uint16_t)((r[2] << 8) | r[3]);
    tag->sak = r[4];
    tag->uid_len = r[5];
    memcpy(tag->uid, r + 6, r[5]);
    *found = true;
    return STARK_OK;
}

stark_err_t nfc_reader_reselect(pn532_t *d, const nfc_tag_t *expect)
{
    bool found = false;
    nfc_tag_t now;
    STARK_CHECK_RET(nfc_reader_poll(d, &found, &now));
    if (!found) {
        return STARK_ERR_NOT_FOUND;
    }
    if (now.uid_len != expect->uid_len || memcmp(now.uid, expect->uid, now.uid_len) != 0) {
        return STARK_ERR_STATE;
    }
    return STARK_OK;
}

stark_err_t nfc_reader_exchange(pn532_t *d, const uint8_t *cmd, size_t n, uint8_t *out, size_t cap,
                                size_t *got)
{
    uint8_t p[1 + 6];
    uint8_t r[1 + 32];
    size_t rgot = 0;
    if (n > sizeof p - 1 || cap > sizeof r - 1) {
        return STARK_ERR_INVALID_ARG;
    }
    p[0] = 0x01; /* Tg */
    memcpy(p + 1, cmd, n);
    STARK_CHECK_RET(pn532_command(d, PN532_CMD_DATA_EXCH, p, n + 1, r, cap + 1, &rgot,
                                  NFC_EXCHANGE_TIMEOUT_MS));
    uint8_t status = rgot > 0 ? r[0] : 0xFFu; /* no status byte at all: refused */
    if (status != 0) {
        return (status & 0x3Fu) == STATUS_TIMEOUT ? STARK_ERR_NOT_FOUND : STARK_ERR_IO;
    }
    *got = rgot - 1;
    memcpy(out, r + 1, *got);
    return STARK_OK;
}

void nfc_reader_release(pn532_t *d)
{
    static const uint8_t p[] = {0x00}; /* every target */
    uint8_t r[1];
    size_t got = 0;
    (void)pn532_command(d, PN532_CMD_RELEASE, p, sizeof p, r, sizeof r, &got,
                        NFC_EXCHANGE_TIMEOUT_MS);
}

/* READ: 16 bytes from page on. */
static stark_err_t read4(pn532_t *d, size_t page, uint8_t out[16])
{
    const uint8_t cmd[] = {NFC_T2_READ, (uint8_t)page};
    size_t got = 0;
    STARK_CHECK_RET(nfc_reader_exchange(d, cmd, sizeof cmd, out, 16, &got));
    return got == 16 ? STARK_OK : STARK_ERR_IO;
}

/* Fills area up to want bytes (data-area offsets), 4 pages at a time. */
static stark_err_t fill(pn532_t *d, uint8_t *area, size_t *avail, size_t want, size_t total)
{
    while (*avail < want) {
        uint8_t buf[16];
        STARK_CHECK_RET(read4(d, NFC_T2_DATA_PAGE + *avail / 4, buf));
        size_t n = total - *avail < sizeof buf ? total - *avail : sizeof buf;
        memcpy(area + *avail, buf, n);
        *avail += n;
    }
    return STARK_OK;
}

/* The CC and the NDEF TLV's value of a Type 2 tag, into out. */
static stark_err_t read_type2(pn532_t *d, stark_nfc_read_t *out)
{
    uint8_t buf[16];
    STARK_CHECK_RET(read4(d, 3, buf)); /* CC + the data area's first 12 bytes */
    nfc_t2_cc_t cc = nfc_t2_cc(buf);
    if (!cc.valid) {
        return STARK_OK; /* not NDEF-formatted: nothing to read, not an error */
    }
    out->writable = cc.writable;
    out->ndef_capacity = nfc_t2_ndef_capacity(cc.data_bytes);
    if (out->tag.pages == 0) {
        out->tag.pages = (uint16_t)(NFC_T2_DATA_PAGE + cc.data_bytes / 4u);
    }
    if (cc.data_bytes > NFC_NDEF_MAX) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    size_t total = cc.data_bytes;
    uint8_t *area = out->ndef;
    size_t avail = total < 12 ? total : 12;
    memcpy(area, buf + 4, avail);
    for (;;) {
        size_t off = 0;
        size_t len = 0;
        size_t need = 0;
        switch (nfc_t2_tlv(area, avail, total, &off, &len, &need)) {
            case NFC_TLV_MORE:
                STARK_CHECK_RET(fill(d, area, &avail, need, total));
                break;
            case NFC_TLV_FOUND:
                STARK_CHECK_RET(fill(d, area, &avail, off + len, total));
                memmove(area, area + off, len);
                out->ndef_len = (uint16_t)len;
                out->ndef_present = true;
                return STARK_OK;
            case NFC_TLV_BAD:
                return STARK_ERR_INVALID_ARG;
            case NFC_TLV_NONE:
            default:
                return STARK_OK;
        }
    }
}

void nfc_reader_read(pn532_t *d, const nfc_tag_t *found, stark_nfc_read_t *out)
{
    memset(out, 0, sizeof *out);
    out->tag = *found;
    uint8_t version[8];
    bool have_version = false;
    if (found->sak == 0x00) {
        static const uint8_t cmd[] = {NFC_T2_VERSION};
        size_t got = 0;
        stark_err_t err = nfc_reader_exchange(d, cmd, sizeof cmd, version, sizeof version, &got);
        if (err == STARK_OK && got == sizeof version) {
            have_version = true;
        } else if (err == STARK_ERR_IO) {
            /* a NAK: the tag went back to IDLE; select it again */
            err = nfc_reader_reselect(d, found);
        }
        out->read_err = err == STARK_ERR_STATE ? STARK_ERR_NOT_FOUND : err;
    }
    out->tag.tech = nfc_ident(found->atqa, found->sak, have_version ? version : NULL);
    out->tag.pages = nfc_tech_pages(out->tag.tech);
    if (out->read_err == STARK_OK && nfc_tech_is_type2(out->tag.tech)) {
        out->read_err = read_type2(d, out);
    }
    if (out->read_err != STARK_OK) {
        out->ndef_present = false;
        out->ndef_len = 0;
        memset(out->ndef, 0, sizeof out->ndef); /* nothing half-read is left */
    }
    nfc_reader_release(d);
}

static stark_err_t dump_pages(pn532_t *d, const nfc_tag_t *tag, nfc_dump_t *out)
{
    STARK_CHECK_RET(nfc_reader_reselect(d, tag));
    if (!nfc_tech_is_type2(tag->tech)) {
        return STARK_OK;
    }
    size_t pages = tag->pages < NFC_T2_MAX_PAGES ? tag->pages : NFC_T2_MAX_PAGES;
    for (size_t page = 0; page < pages; page += 4) {
        uint8_t buf[16];
        STARK_CHECK_RET(read4(d, page, buf));
        size_t n = pages - page < 4 ? pages - page : 4;
        memcpy(out->mem + page * NFC_T2_PAGE_BYTES, buf, n * NFC_T2_PAGE_BYTES);
        out->n_pages = (uint16_t)(page + n);
    }
    return STARK_OK;
}

stark_err_t nfc_reader_dump(pn532_t *d, const nfc_tag_t *tag, nfc_dump_t *out)
{
    memset(out, 0, sizeof *out);
    out->tag = *tag;
    stark_err_t err = dump_pages(d, tag, out);
    nfc_reader_release(d);
    return err;
}
