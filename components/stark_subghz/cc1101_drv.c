/*
 * cc1101_drv.c — the CC1101 driver (cc1101_drv.h). Pure.
 *
 * NOTE: status registers 0x30..0x3D read as strobes unless the burst bit is
 * set, so every status read here uses 0xC0 | addr (datasheet, "Status
 * register details").
 */
#include "cc1101_drv.h"
#include <string.h>

void cc1101_bind(cc1101_t *d, const cc1101_io_t *io)
{
    memset(d, 0, sizeof *d);
    d->io = *io;
}

static stark_err_t xfer(cc1101_t *d, const uint8_t *tx, uint8_t *rx, size_t len)
{
    stark_err_t err = d->io.xfer(d->io.ctx, tx, rx, len);
    if (err == STARK_OK) {
        d->status = rx[0];
    }
    return err;
}

static stark_err_t strobe_raw(cc1101_t *d, uint8_t strobe)
{
    uint8_t rx[1];
    return xfer(d, &strobe, rx, 1);
}

static stark_err_t write_burst(cc1101_t *d, uint8_t addr, const uint8_t *v, size_t n)
{
    uint8_t tx[CC1101_XFER_MAX];
    uint8_t rx[CC1101_XFER_MAX];
    tx[0] = (uint8_t)(addr | (n > 1 ? CC1101_HDR_BURST : 0u));
    memcpy(tx + 1, v, n);
    return xfer(d, tx, rx, 1 + n);
}

/* hdr: CC1101_HDR_READ plus the burst bit and address; n data bytes follow the status byte. */
static stark_err_t read_n(cc1101_t *d, uint8_t hdr, uint8_t *out, size_t n)
{
    uint8_t tx[CC1101_XFER_MAX] = {hdr};
    uint8_t rx[CC1101_XFER_MAX];
    stark_err_t err = xfer(d, tx, rx, 1 + n);
    if (err == STARK_OK) {
        memcpy(out, rx + 1, n);
    }
    return err;
}

static stark_err_t read_status(cc1101_t *d, uint8_t addr, uint8_t *v)
{
    return read_n(d, (uint8_t)(CC1101_HDR_READ | CC1101_HDR_BURST | addr), v, 1);
}

stark_err_t cc1101_marcstate(cc1101_t *d, uint8_t *state)
{
    uint8_t v;
    stark_err_t err = read_status(d, CC1101_MARCSTATE, &v);
    *state = err == STARK_OK ? (uint8_t)(v & CC1101_MARC_MASK) : 0xFFu;
    return err;
}

stark_err_t cc1101_rssi_raw(cc1101_t *d, uint8_t *raw)
{
    uint8_t v = 0xFF;
    stark_err_t err = read_status(d, CC1101_RSSI, &v);
    *raw = v;
    return err;
}

/* Polls MARCSTATE until it equals want: a deadline on the clock and a poll limit. */
static stark_err_t wait_state(cc1101_t *d, uint8_t want, uint32_t timeout_ms)
{
    uint32_t start = d->io.now_ms(d->io.ctx);
    for (unsigned polls = 0; polls < CC1101_MAX_POLLS; polls++) {
        uint8_t state;
        stark_err_t err = cc1101_marcstate(d, &state);
        if (err != STARK_OK) {
            return err;
        }
        if (state == want) {
            return STARK_OK;
        }
        if (d->io.now_ms(d->io.ctx) - start >= timeout_ms) {
            break;
        }
        d->io.delay_ms(d->io.ctx, 1);
    }
    return STARK_ERR_TIMEOUT;
}

stark_err_t cc1101_reset(cc1101_t *d)
{
    d->configured = false;
    stark_err_t err = strobe_raw(d, CC1101_SRES);
    return err != STARK_OK ? err : wait_state(d, CC1101_MARC_IDLE, CC1101_RESET_TIMEOUT_MS);
}

stark_err_t cc1101_idle(cc1101_t *d)
{
    stark_err_t err = strobe_raw(d, CC1101_SIDLE);
    return err != STARK_OK ? err : wait_state(d, CC1101_MARC_IDLE, CC1101_IDLE_TIMEOUT_MS);
}

stark_err_t cc1101_tune_rx(cc1101_t *d, uint32_t hz)
{
    uint8_t freq[3];
    stark_err_t err = cc1101_freq_regs(hz, freq);
    if (err != STARK_OK) {
        return err; /* nothing has been sent */
    }
    if (!d->configured) {
        return STARK_ERR_STATE;
    }
    err = cc1101_idle(d); /* the frequency is only ever written in IDLE */
    if (err == STARK_OK) {
        err = write_burst(d, CC1101_FREQ2, freq, sizeof freq);
    }
    if (err == STARK_OK) {
        err = strobe_raw(d, CC1101_SRX);
    }
    return err != STARK_OK ? err : wait_state(d, CC1101_MARC_RX, CC1101_IDLE_TIMEOUT_MS);
}

stark_err_t cc1101_detect(cc1101_t *d, uint8_t *partnum, uint8_t *version)
{
    *partnum = 0xFF;
    *version = 0xFF;
    uint8_t p = 0xFF;
    uint8_t v = 0xFF;
    stark_err_t err = read_status(d, CC1101_PARTNUM, &p);
    if (err == STARK_OK) {
        err = read_status(d, CC1101_VERSION, &v);
    }
    if (err != STARK_OK) {
        return err;
    }
    *partnum = p;
    *version = v;
    bool ok = p == CC1101_PARTNUM_VALUE && (v == CC1101_VERSION_A || v == CC1101_VERSION_B);
    return ok ? STARK_OK : STARK_ERR_NOT_FOUND;
}

stark_err_t cc1101_configure(cc1101_t *d, cc1101_preset_t preset, uint32_t hz, uint32_t baud)
{
    uint8_t freq[3];
    uint8_t drate_e;
    uint8_t drate_m;
    const cc1101_reg_t *regs;
    size_t n = cc1101_preset_regs(preset, &regs);
    stark_err_t err = cc1101_freq_regs(hz, freq);
    if (err == STARK_OK) {
        err = cc1101_rate_regs(baud, &drate_e, &drate_m);
    }
    if (err != STARK_OK || n == 0) {
        return STARK_ERR_INVALID_ARG; /* nothing has been sent */
    }
    d->configured = false;
    err = cc1101_idle(d);
    for (size_t i = 0; err == STARK_OK && i < n; i++) {
        uint8_t v = regs[i].value;
        switch (regs[i].addr) {
            case CC1101_FREQ2: /* one burst: the three bytes never mix with an old word */
                err = write_burst(d, CC1101_FREQ2, freq, sizeof freq);
                continue;
            case CC1101_FREQ1:
            case CC1101_FREQ0:
                continue;
            case CC1101_MDMCFG4:
                v = (uint8_t)((v & 0xF0u) | drate_e);
                break;
            case CC1101_MDMCFG3:
                v = drate_m;
                break;
            default:
                break;
        }
        err = write_burst(d, regs[i].addr, &v, 1);
    }
    if (err == STARK_OK) {
        err = write_burst(d, CC1101_PATABLE, cc1101_patable(), CC1101_PATABLE_SIZE);
    }
    uint8_t back[3];
    if (err == STARK_OK) {
        err = read_n(d, (uint8_t)(CC1101_HDR_READ | CC1101_HDR_BURST | CC1101_FREQ2), back,
                     sizeof back);
    }
    if (err == STARK_OK && memcmp(back, freq, sizeof freq) != 0) {
        err = STARK_ERR_IO;
    }
    d->configured = err == STARK_OK;
    d->preset = preset;
    d->freq_hz = hz;
    return err;
}

stark_err_t cc1101_tx_start(cc1101_t *d, uint32_t hz)
{
    uint8_t freq[3];
    stark_err_t err = cc1101_freq_regs(hz, freq); /* the band lock, once more, before anything */
    if (err != STARK_OK) {
        return err;
    }
    if (!d->configured || d->preset != CC1101_PRESET_OOK_TX || d->freq_hz != hz) {
        return STARK_ERR_STATE;
    }
    uint8_t back[CC1101_PATABLE_SIZE];
    err =
        read_n(d, (uint8_t)(CC1101_HDR_READ | CC1101_HDR_BURST | CC1101_FREQ2), back, sizeof freq);
    if (err == STARK_OK && memcmp(back, freq, sizeof freq) != 0) {
        err = STARK_ERR_IO;
    }
    if (err == STARK_OK) {
        err = read_n(d, (uint8_t)(CC1101_HDR_READ | CC1101_HDR_BURST | CC1101_PATABLE), back,
                     sizeof back);
    }
    if (err == STARK_OK && memcmp(back, cc1101_patable(), sizeof back) != 0) {
        err = STARK_ERR_IO;
    }
    if (err == STARK_OK) {
        err = strobe_raw(d, CC1101_STX);
    }
    return err != STARK_OK ? err : wait_state(d, CC1101_MARC_TX, CC1101_TX_TIMEOUT_MS);
}

stark_err_t cc1101_strobe(cc1101_t *d, uint8_t strobe)
{
    switch (strobe) {
        case CC1101_SRES:
            d->configured = false;
            return strobe_raw(d, strobe);
        case CC1101_SRX:
            return d->configured ? strobe_raw(d, strobe) : STARK_ERR_STATE;
        case CC1101_SIDLE:
        case CC1101_SFRX:
        case CC1101_SFTX:
        case CC1101_SNOP:
            return strobe_raw(d, strobe);
        case CC1101_STX:
            return STARK_ERR_NOT_SUPPORTED; /* NOTE: only cc1101_tx_start() transmits */
        default:
            return STARK_ERR_INVALID_ARG;
    }
}
