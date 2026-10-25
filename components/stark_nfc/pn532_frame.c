/*
 * pn532_frame.c — PN532 frames (pn532_frame.h). Pure.
 */
#include "pn532_frame.h"
#include <stdbool.h>
#include <string.h>

const uint8_t PN532_ACK[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
const uint8_t PN532_NACK[6] = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};

size_t pn532_frame_build(uint8_t cmd, const uint8_t *params, size_t n, uint8_t *out, size_t cap)
{
    if (out == NULL || (n > 0 && params == NULL) || n > PN532_DATA_MAX - 2 ||
        cap < n + 2 + PN532_FRAME_OVERHEAD) {
        return 0;
    }
    uint8_t len = (uint8_t)(n + 2);
    uint8_t sum = (uint8_t)(PN532_TFI_HOST + cmd);
    out[0] = 0x00;
    out[1] = 0x00;
    out[2] = 0xFF;
    out[3] = len;
    out[4] = (uint8_t)(0x100u - len);
    out[5] = PN532_TFI_HOST;
    out[6] = cmd;
    for (size_t i = 0; i < n; i++) {
        out[7 + i] = params[i];
        sum = (uint8_t)(sum + params[i]);
    }
    out[7 + n] = (uint8_t)(0x100u - sum);
    out[8 + n] = 0x00;
    return n + 2 + PN532_FRAME_OVERHEAD;
}

stark_err_t pn532_frame_parse(const uint8_t *buf, size_t len, pn532_rx_t *kind,
                              const uint8_t **data, size_t *dlen)
{
    if (buf == NULL || kind == NULL || data == NULL || dlen == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    *data = NULL;
    *dlen = 0;
    if (len < 6 || buf[0] != 0x00 || buf[1] != 0x00 || buf[2] != 0xFF) {
        return STARK_ERR_INVALID_ARG;
    }
    if (memcmp(buf, PN532_ACK, sizeof PN532_ACK) == 0) {
        *kind = PN532_RX_ACK;
        return STARK_OK;
    }
    if (memcmp(buf, PN532_NACK, sizeof PN532_NACK) == 0) {
        *kind = PN532_RX_NACK;
        return STARK_OK;
    }
    uint8_t n = buf[3];
    if (n == 0xFF && buf[4] == 0xFF) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if ((uint8_t)(n + buf[4]) != 0 || n == 0) {
        return STARK_ERR_IO;
    }
    if (len < (size_t)n + PN532_FRAME_OVERHEAD) {
        return STARK_ERR_INVALID_ARG;
    }
    uint8_t sum = 0;
    for (size_t i = 0; i <= n; i++) { /* TFI, PD... and DCS */
        sum = (uint8_t)(sum + buf[5 + i]);
    }
    if (sum != 0) {
        return STARK_ERR_IO;
    }
    if (buf[6 + n] != 0x00) {
        return STARK_ERR_INVALID_ARG;
    }
    if (n == 1 && buf[5] == 0x7F) {
        *kind = PN532_RX_APP_ERROR;
        return STARK_OK;
    }
    *kind = PN532_RX_DATA;
    *data = buf + 5;
    *dlen = n;
    return STARK_OK;
}
