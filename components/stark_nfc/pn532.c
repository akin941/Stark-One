/*
 * pn532.c — the PN532 protocol driver (pn532.h). Pure.
 */
#include "pn532.h"
#include <string.h>

void pn532_bind(pn532_t *d, const pn532_io_t *io)
{
    memset(d, 0, sizeof *d);
    d->io = *io;
}

static stark_err_t bus(pn532_t *d, stark_err_t err)
{
    if (err != STARK_OK && err != STARK_ERR_NOT_FOUND) {
        d->stats.bus_errors++;
    }
    return err;
}

/* Polls the status byte until the chip has a frame, then reads status + len
 * bytes of it into d->buf. */
static stark_err_t read_ready(pn532_t *d, size_t len, uint32_t timeout_ms)
{
    uint32_t start = d->io.now_ms(d->io.ctx);
    for (;;) {
        uint8_t status = 0;
        stark_err_t err = bus(d, d->io.read(d->io.ctx, &status, 1));
        if (err != STARK_OK) {
            return err;
        }
        if ((status & 0x01u) != 0) {
            memset(d->buf, 0, 1 + len);
            STARK_CHECK_RET(bus(d, d->io.read(d->io.ctx, d->buf, 1 + len)));
            if ((d->buf[0] & 0x01u) != 0) {
                return STARK_OK;
            }
        }
        if (d->io.now_ms(d->io.ctx) - start >= timeout_ms) {
            d->stats.timeouts++;
            return STARK_ERR_TIMEOUT;
        }
        d->io.delay_ms(d->io.ctx, PN532_POLL_MS);
    }
}

/* Writes the command frame and waits for its ACK, repeating after a NACK, a
 * bad ACK or no ACK at all. */
static stark_err_t send_acked(pn532_t *d, const uint8_t *frame, size_t flen)
{
    stark_err_t err = STARK_ERR_IO;
    for (unsigned attempt = 0; attempt <= PN532_RETRIES; attempt++) {
        if (attempt > 0) {
            d->stats.retries++;
        }
        err = bus(d, d->io.write(d->io.ctx, frame, flen));
        if (err == STARK_ERR_NOT_FOUND) {
            return err;
        }
        if (err != STARK_OK) {
            continue;
        }
        err = read_ready(d, sizeof PN532_ACK, PN532_ACK_TIMEOUT_MS);
        if (err == STARK_ERR_NOT_FOUND) {
            return err;
        }
        if (err != STARK_OK) {
            continue;
        }
        pn532_rx_t kind;
        const uint8_t *data;
        size_t dlen;
        if (pn532_frame_parse(d->buf + 1, sizeof PN532_ACK, &kind, &data, &dlen) == STARK_OK &&
            kind == PN532_RX_ACK) {
            return STARK_OK;
        }
        err = STARK_ERR_IO; /* a NACK or a corrupt ACK */
    }
    return err;
}

stark_err_t pn532_command(pn532_t *d, uint8_t cmd, const uint8_t *params, size_t n, uint8_t *resp,
                          size_t cap, size_t *got, uint32_t timeout_ms)
{
    if (d == NULL || got == NULL || (cap > 0 && resp == NULL) || cap > PN532_DATA_MAX - 2) {
        return STARK_ERR_INVALID_ARG;
    }
    *got = 0;
    uint8_t frame[PN532_DATA_MAX + PN532_FRAME_OVERHEAD];
    size_t flen = pn532_frame_build(cmd, params, n, frame, sizeof frame);
    if (flen == 0) {
        return STARK_ERR_INVALID_ARG;
    }
    STARK_CHECK_RET(send_acked(d, frame, flen));
    size_t want = cap + 2 + PN532_FRAME_OVERHEAD; /* TFI + code + data, framed */
    for (unsigned resend = 0;; resend++) {
        stark_err_t err = read_ready(d, want, timeout_ms);
        if (err == STARK_ERR_TIMEOUT) {
            (void)bus(d, d->io.write(d->io.ctx, PN532_ACK, sizeof PN532_ACK)); /* abort */
        }
        STARK_CHECK_RET(err);
        pn532_rx_t kind;
        const uint8_t *data;
        size_t dlen;
        err = pn532_frame_parse(d->buf + 1, want, &kind, &data, &dlen);
        if (err == STARK_ERR_NOT_SUPPORTED) {
            return err;
        }
        if (err != STARK_OK) {
            if (err == STARK_ERR_IO) {
                d->stats.checksum_errors++;
            }
            if (resend == PN532_RESENDS) {
                return STARK_ERR_IO;
            }
            d->stats.naks_sent++;
            STARK_CHECK_RET(bus(d, d->io.write(d->io.ctx, PN532_NACK, sizeof PN532_NACK)));
            continue;
        }
        if (kind != PN532_RX_DATA || dlen < 2 || data[0] != PN532_TFI_CHIP ||
            data[1] != (uint8_t)(cmd + 1)) {
            return STARK_ERR_IO;
        }
        if (dlen > 2) {
            memcpy(resp, data + 2, dlen - 2);
        }
        *got = dlen - 2;
        return STARK_OK;
    }
}

stark_err_t pn532_firmware(pn532_t *d, uint8_t *ic, uint8_t *ver, uint8_t *rev)
{
    uint8_t r[4] = {0}; /* a short answer reads as IC 0: not a PN532 */
    size_t got = 0;
    STARK_CHECK_RET(pn532_command(d, PN532_CMD_GET_FIRMWARE, NULL, 0, r, sizeof r, &got, 100));
    *ic = r[0];
    *ver = r[1];
    *rev = r[2];
    return STARK_OK;
}

stark_err_t pn532_sam_normal(pn532_t *d)
{
    static const uint8_t p[] = {0x01, 0x14, 0x01}; /* normal mode, 1 s timeout, IRQ on */
    size_t got = 0;
    return pn532_command(d, PN532_CMD_SAM_CONFIG, p, sizeof p, NULL, 0, &got, 100);
}

stark_err_t pn532_field(pn532_t *d, bool on)
{
    const uint8_t p[] = {0x01, (uint8_t)(on ? 1 : 0)}; /* item 1: RF field */
    size_t got = 0;
    return pn532_command(d, PN532_CMD_RF_CONFIG, p, sizeof p, NULL, 0, &got, 100);
}

stark_err_t pn532_passive_retries(pn532_t *d, uint8_t n)
{
    const uint8_t p[] = {0x05, 0xFF, 0x01, n}; /* item 5: ATR_RES, PSL, passive activation */
    size_t got = 0;
    return pn532_command(d, PN532_CMD_RF_CONFIG, p, sizeof p, NULL, 0, &got, 100);
}

stark_err_t pn532_detect(pn532_t *d, uint8_t *ic, uint8_t *ver, uint8_t *rev)
{
    STARK_CHECK_RET(pn532_firmware(d, ic, ver, rev));
    if (*ic != PN532_IC) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    STARK_CHECK_RET(pn532_sam_normal(d));
    STARK_CHECK_RET(pn532_passive_retries(d, 1));
    return pn532_field(d, false);
}
