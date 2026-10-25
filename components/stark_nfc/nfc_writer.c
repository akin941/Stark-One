/*
 * nfc_writer.c — writing an NDEF message to a Type 2 tag (nfc_writer.h).
 * Pure.
 */
#include "nfc_writer.h"
#include <string.h>
#include "nfc_reader.h"
#include "nfc_type2.h"

static stark_err_t read4(pn532_t *d, size_t page, uint8_t out[16])
{
    const uint8_t cmd[] = {NFC_T2_READ, (uint8_t)page};
    size_t got = 0;
    STARK_CHECK_RET(nfc_reader_exchange(d, cmd, sizeof cmd, out, 16, &got));
    return got == 16 ? STARK_OK : STARK_ERR_IO;
}

static stark_err_t write_all(pn532_t *d, const nfc_tag_t *expect, const uint8_t *msg, size_t len,
                             uint16_t *pages)
{
    STARK_CHECK_RET(nfc_reader_reselect(d, expect));
    uint8_t buf[16];
    STARK_CHECK_RET(read4(d, 3, buf));
    nfc_t2_cc_t cc = nfc_t2_cc(buf);
    if (!cc.valid || !cc.writable) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    size_t n = nfc_t2_tlv_size(len);
    if (n == 0 || n > cc.data_bytes) {
        return STARK_ERR_NO_SPACE;
    }
    for (size_t page = 0; page < n / 4; page++) {
        uint8_t cmd[6] = {NFC_T2_WRITE, (uint8_t)(NFC_T2_DATA_PAGE + page)};
        nfc_t2_tlv_page(msg, len, page, cmd + 2);
        size_t got = 0;
        STARK_CHECK_RET(nfc_reader_exchange(d, cmd, sizeof cmd, buf, 0, &got));
        (*pages)++;
    }
    for (size_t page = 0; page < n / 4; page += 4) { /* read back, 4 pages per READ */
        STARK_CHECK_RET(read4(d, NFC_T2_DATA_PAGE + page, buf));
        for (size_t k = 0; k < 4 && page + k < n / 4; k++) {
            uint8_t want[4];
            nfc_t2_tlv_page(msg, len, page + k, want);
            if (memcmp(buf + 4 * k, want, 4) != 0) {
                return STARK_ERR_IO;
            }
        }
    }
    return STARK_OK;
}

stark_err_t nfc_writer_write(pn532_t *d, const nfc_tag_t *expect, const uint8_t *msg, size_t len,
                             uint16_t *pages)
{
    *pages = 0;
    stark_err_t err = write_all(d, expect, msg, len, pages);
    nfc_reader_release(d);
    return err;
}
