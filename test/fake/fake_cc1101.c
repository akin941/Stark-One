/*
 * fake_cc1101.c — a test-only CC1101 (fake_cc1101.h). Pure.
 *
 * NOTE: nothing here includes the driver's headers: the register numbers, the
 * band edges and the RSSI encoding are written out again from the datasheet, so
 * a mistake shared by the driver and the fake cannot cancel out. The two meet
 * only on the bytes.
 */
#include "fake_cc1101.h"
#include <stdlib.h>
#include <string.h>

#define REG_COUNT  0x2F
#define BAND_W_MIN 1091552u /* round(433.05e6 * 65536 / 26e6) */
#define BAND_W_MAX 1095938u /* round(434.79e6 * 65536 / 26e6) */
#define ST_IDLE    0u
#define ST_RX      1u
#define ST_TX      2u
#define MARC_IDLE  0x01u
#define MARC_RX    0x0Du
#define MARC_TX    0x13u

/* Datasheet power-on values of 0x00..0x2E. */
static const uint8_t k_defaults[REG_COUNT] = {
    0x29, 0x2E, 0x3F, 0x07, 0xD3, 0x91, 0xFF, 0x04, 0x45, 0x00, 0x00, 0x0F, 0x00, 0x1E, 0xC4, 0xEC,
    0x8C, 0x22, 0x02, 0x22, 0xF8, 0x47, 0x07, 0x30, 0x04, 0x36, 0x6C, 0x03, 0x40, 0x91, 0x87, 0x6B,
    0xFB, 0xB6, 0x10, 0xA9, 0x0A, 0x20, 0x0D, 0x41, 0x00, 0x59, 0x7F, 0x3F, 0x88, 0x31, 0x0B};

static struct {
    uint8_t reg[REG_COUNT];
    uint8_t patable[8];
    uint8_t pa_idx;
    uint8_t marc;
    bool present;
    uint8_t partnum;
    uint8_t version;
    fake_cc1101_fault_t fault;
    int fault_count;
    uint32_t air_hz;
    int air_dbm;
    uint8_t rssi;
    const subghz_sym_t *scene; /* the caller's (or rec): never copied */
    size_t scene_n;
    size_t scene_pos;
    bool scene_armed;
    subghz_sym_t *rec; /* allocated at the first transmission: most runs never need it */
    size_t rec_n;
    bool own_rx;
    uint32_t freq_log[FAKE_CC1101_FREQ_LOG];
    size_t freq_log_n;
    fake_cc1101_stats_t st;
} s;

static void defaults(void)
{
    memcpy(s.reg, k_defaults, sizeof s.reg);
    memset(s.patable, 0, sizeof s.patable);
    s.patable[0] = 0xC6;
    s.pa_idx = 0;
    s.marc = MARC_IDLE;
    s.rssi = 0;
}

void fake_cc1101_reset(void)
{
    free(s.rec);
    memset(&s, 0, sizeof s);
    s.present = true;
    s.partnum = 0x00;
    s.version = 0x14;
    defaults();
}

void fake_cc1101_chip(uint8_t partnum, uint8_t version)
{
    s.partnum = partnum;
    s.version = version;
}

void fake_cc1101_present(bool present)
{
    s.present = present;
}

void fake_cc1101_fault(fake_cc1101_fault_t kind, int count)
{
    s.fault = kind;
    s.fault_count = kind == FAKE_CC1101_NONE ? 0 : count;
}

void fake_cc1101_air(uint32_t carrier_hz, int level_dbm)
{
    s.air_hz = carrier_hz;
    s.air_dbm = level_dbm;
}

/* True once per armed occurrence of fault f. */
static bool take(fake_cc1101_fault_t f)
{
    if (s.fault != f || s.fault_count == 0) {
        return false;
    }
    if (s.fault_count > 0 && --s.fault_count == 0) {
        s.fault = FAKE_CC1101_NONE;
    }
    return true;
}

static uint32_t tuned_word(void)
{
    return ((uint32_t)s.reg[0x0D] << 16) | ((uint32_t)s.reg[0x0E] << 8) | s.reg[0x0F];
}

static uint32_t word_to_hz(uint32_t w)
{
    return (uint32_t)(((uint64_t)w * 26000000u) >> 16);
}

/* The RSSI byte for the air at the tuned frequency, the way the chip encodes it (offset 74). */
static uint8_t rssi_now(void)
{
    uint32_t hz = word_to_hz(tuned_word());
    int dbm = -110;
    if (s.air_hz != 0) {
        uint32_t d = hz > s.air_hz ? hz - s.air_hz : s.air_hz - hz;
        if (d <= FAKE_CC1101_AIR_HALF_WIDTH_HZ) {
            dbm = s.air_dbm;
        }
    }
    int half = (dbm + 74) * 2;
    half = half > 127 ? 127 : (half < -128 ? -128 : half);
    return (uint8_t)(half >= 0 ? half : half + 256);
}

static uint8_t status_reg(uint8_t addr)
{
    switch (addr) {
        case 0x30:
            return s.partnum;
        case 0x31:
            return s.version;
        case 0x34:
            if (s.marc == MARC_RX) {
                s.rssi = rssi_now();
            }
            return s.rssi;
        case 0x35:
            return s.marc;
        default:
            return 0;
    }
}

static void goto_state(uint8_t marc, bool stuck)
{
    if (stuck) {
        return;
    }
    s.marc = marc;
    if (marc == MARC_RX && s.scene_n > 0) {
        s.scene_armed = true;
        s.scene_pos = 0;
    }
    if (marc == MARC_TX) {
        s.rec_n = 0;
        if (s.rec == NULL) {
            s.rec = malloc(FAKE_CC1101_SCENE_MAX * sizeof s.rec[0]);
        }
    }
}

static void strobe(uint8_t cmd, bool stuck)
{
    switch (cmd) {
        case 0x30: /* SRES */
        {
            uint8_t was = s.marc;
            defaults();
            if (stuck) {
                s.marc = was; /* wedged where it was */
            }
            break;
        }
        case 0x34: /* SRX */
            goto_state(MARC_RX, stuck);
            break;
        case 0x35: /* STX */
            s.st.tx_strobes++;
            goto_state(MARC_TX, stuck);
            break;
        case 0x36: /* SIDLE */
            goto_state(MARC_IDLE, stuck);
            break;
        default: /* SFRX, SFTX, SNOP and the strobes this chip model does not need */
            break;
    }
}

static void pa_write(uint8_t b)
{
    s.st.patable_writes++;
    if (b > s.st.patable_max) {
        s.st.patable_max = b;
    }
    if (b != 0x00 && b != 0x60) {
        s.st.patable_other++;
    }
    s.patable[s.pa_idx++ & 7u] = b;
}

stark_err_t fake_cc1101_xfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (tx == NULL || rx == NULL || len == 0) {
        return STARK_ERR_INVALID_ARG;
    }
    s.st.transfers++;
    if (take(FAKE_CC1101_SPI_ERROR)) {
        return STARK_ERR_IO;
    }
    if (!s.present || take(FAKE_CC1101_NO_ANSWER)) {
        memset(rx, 0xFF, len);
        return STARK_OK;
    }
    bool stuck = take(FAKE_CC1101_STUCK);
    uint8_t state = s.marc == MARC_RX ? ST_RX : (s.marc == MARC_TX ? ST_TX : ST_IDLE);
    memset(rx, (int)(state << 4), len);
    uint8_t hdr = tx[0];
    bool read = (hdr & 0x80u) != 0;
    bool burst = (hdr & 0x40u) != 0;
    uint8_t addr = hdr & 0x3Fu;
    if (addr < REG_COUNT) {
        bool freq = false;
        for (size_t i = 1; i < len; i++) {
            size_t a = (size_t)addr + (burst ? i - 1 : 0);
            if (a >= REG_COUNT || (!burst && i > 1)) {
                break; /* a single access is one data byte; a burst stops at the last register */
            }
            if (read) {
                rx[i] = s.reg[a];
            } else {
                s.reg[a] = tx[i];
                freq = freq || (a >= 0x0D && a <= 0x0F);
            }
        }
        if (freq) {
            uint32_t w = tuned_word();
            s.st.freq_writes++;
            s.st.out_of_band += (w < BAND_W_MIN || w > BAND_W_MAX) ? 1u : 0u;
            if (s.freq_log_n == FAKE_CC1101_FREQ_LOG) {
                memmove(s.freq_log, s.freq_log + 1, (FAKE_CC1101_FREQ_LOG - 1) * sizeof(uint32_t));
                s.freq_log_n--;
            }
            s.freq_log[s.freq_log_n++] = word_to_hz(w);
        }
    } else if (addr == 0x3E) {
        s.pa_idx = 0;
        for (size_t i = 1; i < len; i++) {
            if (read) {
                rx[i] = s.patable[s.pa_idx++ & 7u];
            } else {
                pa_write(tx[i]);
            }
        }
    } else if (addr >= 0x30 && addr <= 0x3D) {
        if (read && burst) {
            for (size_t i = 1; i < len; i++) {
                rx[i] = status_reg((uint8_t)(addr + i - 1));
            }
            if (take(FAKE_CC1101_BIT_FLIP) && len > 1) {
                rx[1] ^= 1u;
            }
        } else {
            strobe(addr, stuck);
        }
    }
    if (read && addr < REG_COUNT && take(FAKE_CC1101_BIT_FLIP) && len > 1) {
        rx[1] ^= 1u;
    }
    return STARK_OK;
}

void fake_cc1101_scene(const subghz_sym_t *sy, size_t n)
{
    s.scene = sy;
    s.scene_n = sy == NULL ? 0 : (n < FAKE_CC1101_SCENE_MAX ? n : FAKE_CC1101_SCENE_MAX);
    s.scene_pos = 0;
    s.scene_armed = s.scene_n > 0;
}

size_t fake_cc1101_gdo2_take(subghz_sym_t *out, size_t max)
{
    if (s.marc != MARC_RX || !s.scene_armed) {
        return 0;
    }
    size_t left = s.scene_n - s.scene_pos;
    size_t n = left < max ? left : max;
    memcpy(out, s.scene + s.scene_pos, n * sizeof out[0]);
    s.scene_pos += n;
    s.scene_armed = s.scene_pos < s.scene_n;
    return n;
}

bool fake_cc1101_gdo0_put(const subghz_sym_t *sy, size_t n)
{
    if (s.marc != MARC_TX || s.rec == NULL || s.rec_n + n > FAKE_CC1101_SCENE_MAX) {
        return false;
    }
    memcpy(s.rec + s.rec_n, sy, n * sizeof sy[0]);
    s.rec_n += n;
    if (s.own_rx) {
        s.scene = s.rec;
        s.scene_n = s.rec_n;
        s.scene_pos = 0;
        s.scene_armed = true;
    }
    return true;
}

size_t fake_cc1101_gdo0_recorded(const subghz_sym_t **out)
{
    *out = s.rec;
    return s.rec_n;
}

void fake_cc1101_own_rx(bool on)
{
    s.own_rx = on;
}

const fake_cc1101_stats_t *fake_cc1101_stats(void)
{
    s.st.marcstate = s.marc;
    s.st.tuned_hz = word_to_hz(tuned_word());
    memcpy(s.st.reg, s.reg, sizeof s.st.reg);
    memcpy(s.st.patable, s.patable, sizeof s.st.patable);
    return &s.st;
}

size_t fake_cc1101_freq_log(const uint32_t **out)
{
    *out = s.freq_log;
    return s.freq_log_n;
}
