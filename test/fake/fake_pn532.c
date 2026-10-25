/*
 * fake_pn532.c — a test-only PN532 (fake_pn532.h). Pure.
 *
 * NOTE: the frame code here is deliberately not stark_nfc's: a bug shared by
 * the driver and the fake would cancel out. The two meet only on the bytes.
 */
#include "fake_pn532.h"
#include <string.h>

#define FRAME_MAX      300
#define STATUS_OK      0x00u
#define STATUS_TIMEOUT 0x01u /* the target did not answer */
#define STATUS_NAK     0x14u /* the target refused the command */

static const uint8_t k_ack[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
static const uint8_t k_nack[6] = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};
static const uint8_t k_app_error[8] = {0x00, 0x00, 0xFF, 0x01, 0xFF, 0x7F, 0x81, 0x00};

typedef enum { PHASE_IDLE, PHASE_ACK, PHASE_BUSY, PHASE_RESP } phase_t;

static struct {
    fake_fault_t fault;
    int times;
    bool tag_set;
    bool present;
    bool selected;
    bool vanish_armed;
    unsigned vanish_left;
    fake_tag_t tag;
    phase_t phase;
    unsigned busy;
    bool never_ready;
    uint8_t ack[6];
    uint8_t resp[FRAME_MAX];
    size_t resp_len;
    uint8_t last[FRAME_MAX]; /* the clean response, for a host NACK */
    size_t last_len;
    fake_pn532_stats_t st;
} s;

void fake_pn532_reset(void)
{
    memset(&s, 0, sizeof s);
}

void fake_pn532_fault(fake_fault_t f, int times)
{
    s.fault = f;
    s.times = f == FAKE_NONE ? 0 : times;
}

void fake_pn532_tag(const fake_tag_t *t)
{
    s.tag_set = t != NULL;
    s.present = t != NULL;
    s.selected = false;
    s.vanish_armed = false;
    if (t != NULL) {
        s.tag = *t;
    }
}

void fake_pn532_vanish_after(unsigned exchanges)
{
    s.vanish_armed = true;
    s.vanish_left = exchanges;
}

const fake_tag_t *fake_pn532_current_tag(void)
{
    return s.tag_set ? &s.tag : NULL;
}

const fake_pn532_stats_t *fake_pn532_stats(void)
{
    return &s.st;
}

/* Consumes one occurrence of fault f if it is armed. */
static bool take(fake_fault_t f)
{
    if (s.fault != f || s.times == 0) {
        return false;
    }
    if (s.times > 0 && --s.times == 0) {
        s.fault = FAKE_NONE;
    }
    return true;
}

static stark_err_t transfer_fault(void)
{
    if (take(FAKE_NO_DEVICE)) {
        return STARK_ERR_NOT_FOUND;
    }
    if (take(FAKE_BUS_ERROR)) {
        return STARK_ERR_IO;
    }
    return STARK_OK;
}

/* A host information frame: 00 00 FF LEN LCS D4 CMD PD... DCS 00. */
static bool host_frame(const uint8_t *b, size_t len, uint8_t *cmd, const uint8_t **pd, size_t *n)
{
    if (len < 9 || b[0] != 0x00 || b[1] != 0x00 || b[2] != 0xFF) {
        return false;
    }
    unsigned flen = b[3];
    if (flen < 2 || ((flen + b[4]) & 0xFFu) != 0 || len != flen + 7u || b[5] != 0xD4) {
        return false;
    }
    unsigned sum = 0;
    for (unsigned i = 0; i <= flen; i++) {
        sum += b[5 + i];
    }
    if ((sum & 0xFFu) != 0 || b[6 + flen] != 0x00) {
        return false;
    }
    *cmd = b[6];
    *pd = b + 7;
    *n = flen - 2;
    return true;
}

/* InDataExchange to the selected tag: out[0] is the PN532 status byte. */
static size_t exchange(const uint8_t *tx, size_t n, uint8_t *out)
{
    if (!s.selected || !s.present) {
        out[0] = STATUS_TIMEOUT;
        return 1;
    }
    if (s.vanish_armed) {
        if (s.vanish_left == 0) {
            s.present = false;
            s.selected = false;
            s.vanish_armed = false;
            out[0] = STATUS_TIMEOUT;
            return 1;
        }
        s.vanish_left--;
    }
    out[0] = STATUS_NAK;
    if (!s.tag.type2 || n < 1) {
        return 1;
    }
    if (tx[0] == 0x30 && n >= 2 && tx[1] < s.tag.pages) { /* READ: 4 pages, rolling over */
        out[0] = STATUS_OK;
        for (unsigned i = 0; i < 16; i++) {
            unsigned page = (tx[1] + i / 4u) % s.tag.pages;
            out[1 + i] = s.tag.mem[page * 4u + i % 4u];
        }
        return 17;
    }
    if (tx[0] == 0xA2 && n >= 6 && tx[1] < s.tag.pages) { /* WRITE: one page */
        if (s.st.writes < FAKE_MAX_WRITES) {
            s.st.write_page[s.st.writes] = tx[1];
        }
        s.st.writes++;
        if (s.tag.ro_from == 0 || tx[1] < s.tag.ro_from) {
            memcpy(&s.tag.mem[tx[1] * 4u], tx + 2, 4);
        }
        out[0] = STATUS_OK;
        return 1;
    }
    if (tx[0] == 0x60 && s.tag.has_version) { /* GET_VERSION */
        out[0] = STATUS_OK;
        memcpy(out + 1, s.tag.version, 8);
        return 9;
    }
    s.selected = false; /* a NAK sends a Type 2 tag back to IDLE: select it again */
    return 1;
}

/* Runs cmd; returns the response data length, or -1 for an application error. */
static int execute(uint8_t cmd, const uint8_t *pd, size_t n, uint8_t *out)
{
    switch (cmd) {
        case 0x02: /* GetFirmwareVersion */
            out[0] = take(FAKE_OTHER_IC) ? 0x31 : 0x32;
            out[1] = 0x01;
            out[2] = 0x06;
            out[3] = 0x07;
            return 4;
        case 0x14: /* SAMConfiguration */
            return n >= 1 ? 0 : -1;
        case 0x32: /* RFConfiguration */
            if (n >= 2 && pd[0] == 0x01) {
                s.st.field = (pd[1] & 0x01u) != 0;
            } else if (n >= 4 && pd[0] == 0x05) {
                s.st.passive_retries = pd[3];
            }
            return n >= 1 ? 0 : -1;
        case 0x4A: /* InListPassiveTarget, 106 kbit/s type A */
            if (n < 2 || pd[1] != 0x00) {
                return -1;
            }
            if (!s.st.field) {
                s.st.list_field_off++;
            }
            if (!s.st.field || !s.present) {
                out[0] = 0;
                return 1;
            }
            s.selected = true;
            out[0] = 1;
            out[1] = 1;
            out[2] = (uint8_t)(s.tag.atqa >> 8);
            out[3] = (uint8_t)s.tag.atqa;
            out[4] = s.tag.sak;
            out[5] = s.tag.uid_len;
            memcpy(out + 6, s.tag.uid, s.tag.uid_len);
            return 6 + s.tag.uid_len;
        case 0x40: /* InDataExchange: Tg, then the tag command */
            if (n < 1) {
                return -1;
            }
            return (int)exchange(pd + 1, n - 1, out);
        case 0x52: /* InRelease */
            s.selected = false;
            out[0] = STATUS_OK;
            return 1;
        default:
            return -1;
    }
}

/* Frames the response to cmd (or the application error) into s.last. */
static void frame_response(uint8_t cmd, const uint8_t *data, int dn)
{
    if (dn < 0) {
        memcpy(s.last, k_app_error, sizeof k_app_error);
        s.last_len = sizeof k_app_error;
        return;
    }
    unsigned flen = 2u + (unsigned)dn;
    unsigned sum = 0xD5u + cmd + 1u;
    s.last[0] = 0x00;
    s.last[1] = 0x00;
    s.last[2] = 0xFF;
    s.last[3] = (uint8_t)flen;
    s.last[4] = (uint8_t)(0x100u - flen);
    s.last[5] = 0xD5;
    s.last[6] = (uint8_t)(cmd + 1u);
    for (int i = 0; i < dn; i++) {
        s.last[7 + i] = data[i];
        sum += data[i];
    }
    s.last[5 + flen] = (uint8_t)(0x100u - (sum & 0xFFu));
    s.last[6 + flen] = 0x00;
    s.last_len = flen + 7u;
}

/* The response as it leaves the chip this time: the clean frame, or a fault. */
static void corrupt_response(void)
{
    memcpy(s.resp, s.last, s.last_len);
    s.resp_len = s.last_len;
    if (take(FAKE_APP_ERROR)) {
        memcpy(s.resp, k_app_error, sizeof k_app_error);
        s.resp_len = sizeof k_app_error;
    } else if (take(FAKE_EXTENDED)) {
        static const uint8_t ext[] = {0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00,
                                      0x02, 0xFE, 0xD5, 0x03, 0x28, 0x00};
        memcpy(s.resp, ext, sizeof ext);
        s.resp_len = sizeof ext;
    } else if (take(FAKE_WRONG_CODE)) {
        s.resp[6] = (uint8_t)(s.resp[6] + 2u);
        s.resp[s.resp_len - 2] = (uint8_t)(s.resp[s.resp_len - 2] - 2u); /* DCS still valid */
    } else if (take(FAKE_BAD_LCS)) {
        s.resp[4] = (uint8_t)(s.resp[4] ^ 0x01u);
    } else if (take(FAKE_BAD_DCS)) {
        s.resp[s.resp_len - 2] = (uint8_t)(s.resp[s.resp_len - 2] ^ 0x01u);
    } else if (take(FAKE_TRUNCATED)) {
        s.resp_len -= 3;
    }
}

stark_err_t fake_pn532_write(const uint8_t *buf, size_t len)
{
    STARK_CHECK_RET(transfer_fault());
    if (len == sizeof k_ack && memcmp(buf, k_ack, len) == 0) { /* abort */
        s.phase = PHASE_IDLE;
        s.never_ready = false;
        s.st.aborts++;
        return STARK_OK;
    }
    if (len == sizeof k_nack && memcmp(buf, k_nack, len) == 0) { /* send it again */
        if (s.last_len > 0) {
            corrupt_response(); /* an armed fault corrupts the resend too */
            s.phase = PHASE_RESP;
            s.st.host_nacks++;
        }
        return STARK_OK;
    }
    uint8_t cmd;
    const uint8_t *pd;
    size_t n;
    if (!host_frame(buf, len, &cmd, &pd, &n)) {
        s.st.bad_frames++; /* the chip ignores a frame it cannot read */
        return STARK_OK;
    }
    s.st.frames++;
    s.st.last_cmd = cmd;
    if (take(FAKE_SILENT)) {
        s.phase = PHASE_IDLE;
        return STARK_OK;
    }
    memcpy(s.ack, take(FAKE_SEND_NACK) ? k_nack : k_ack, sizeof s.ack);
    if (take(FAKE_BAD_ACK)) {
        s.ack[4] = 0xFE;
    }
    uint8_t data[FRAME_MAX];
    frame_response(cmd, data, execute(cmd, pd, n, data));
    corrupt_response();
    s.never_ready = take(FAKE_NEVER_READY);
    s.phase = PHASE_ACK;
    return STARK_OK;
}

static void deliver(uint8_t *buf, size_t len, const uint8_t *frame, size_t flen)
{
    buf[0] = 0x01;
    memcpy(buf + 1, frame, len - 1 < flen ? len - 1 : flen);
}

stark_err_t fake_pn532_read(uint8_t *buf, size_t len)
{
    STARK_CHECK_RET(transfer_fault());
    if (buf == NULL || len == 0) {
        return STARK_ERR_INVALID_ARG;
    }
    memset(buf, 0, len);
    switch (s.phase) {
        case PHASE_ACK:
            deliver(buf, len, s.ack, sizeof s.ack);
            if (len > 1) { /* a status-only read leaves the frame waiting */
                if (memcmp(s.ack, k_ack, sizeof k_ack) == 0) {
                    s.phase = PHASE_BUSY;
                    s.busy = 1; /* one not-ready poll: the chip is working */
                } else {
                    s.phase = PHASE_IDLE;
                }
            }
            return STARK_OK;
        case PHASE_BUSY:
            if (s.never_ready || s.busy > 0) {
                if (s.busy > 0) {
                    s.busy--;
                }
                return STARK_OK;
            }
            s.phase = PHASE_RESP;
            deliver(buf, len, s.resp, s.resp_len);
            if (len > 1) {
                s.phase = PHASE_IDLE;
            }
            return STARK_OK;
        case PHASE_RESP:
            deliver(buf, len, s.resp, s.resp_len);
            if (len > 1) {
                s.phase = PHASE_IDLE;
            }
            return STARK_OK;
        case PHASE_IDLE:
        default:
            return STARK_OK; /* status 0: nothing ready */
    }
}
