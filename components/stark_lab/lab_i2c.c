/*
 * lab_i2c.c — the I2C scanner's rules (lab_i2c.h). Pure and bounded: a sweep is 112 probes, a
 * dump 16 transactions, a write one; the fingerprint is a byte-at-a-time hash.
 */
#include "lab_i2c.h"
#include <string.h>

#define REG_BYTES_MAX 2u

void lab_i2c_init(lab_i2c_t *c, const lab_i2c_io_t *io, const stark_board_pins_t *pins)
{
    memset(c, 0, sizeof(*c));
    c->io = *io;
    for (size_t i = 0; i < STARK_BOARD_MODULE_SLOTS; i++) {
        c->eeprom[i] = pins->module[i].eeprom_addr;
    }
}

stark_err_t lab_i2c_scan(lab_i2c_t *c, stark_lab_scan_t *out)
{
    memset(out, 0, sizeof(*out));
    unsigned run = 0;
    for (unsigned a = STARK_LAB_I2C_ADDR_MIN; a <= STARK_LAB_I2C_ADDR_MAX; a++) {
        stark_err_t err = c->io.probe(c->io.ctx, (uint8_t)a, LAB_I2C_PROBE_MS);
        if (err == STARK_OK) {
            out->addr[out->n++] = (uint8_t)a;
            run = 0;
        } else if (err == STARK_ERR_NOT_FOUND) {
            run = 0;
        } else {
            out->errors++;
            if (++run >= LAB_I2C_ERR_RUN) {
                out->status = STARK_ERR_IO;
                break;
            }
        }
    }
    return out->status;
}

lab_i2c_refusal_t lab_i2c_refusal(const lab_i2c_t *c, uint8_t addr, size_t wn, size_t rn)
{
    for (size_t i = 0; i < STARK_BOARD_MODULE_SLOTS; i++) {
        if (c->eeprom[i] != 0u && c->eeprom[i] == addr && (rn == 0u || wn > 1u)) {
            return LAB_I2C_REFUSE_EEPROM;
        }
    }
    if (addr == STARK_LAB_I2C_PN532 && c->io.nfc_confirmed(c->io.ctx)) {
        return LAB_I2C_REFUSE_NFC;
    }
    return LAB_I2C_REFUSE_NONE;
}

const char *lab_i2c_refusal_text(lab_i2c_refusal_t why)
{
    switch (why) {
        case LAB_I2C_REFUSE_EEPROM:
            return "module eeprom";
        case LAB_I2C_REFUSE_NFC:
            return "nfc reader";
        case LAB_I2C_REFUSE_NONE:
        default:
            return "";
    }
}

/* The register as it goes on the wire: one byte, or two with the high one first. */
static size_t put_reg(uint8_t *w, uint16_t reg, bool reg16)
{
    size_t n = 0;
    if (reg16) {
        w[n++] = (uint8_t)(reg >> 8);
    }
    w[n++] = (uint8_t)(reg & 0xFFu);
    return n;
}

static bool args_ok(uint8_t addr, uint16_t reg, bool reg16, uint32_t hz)
{
    return addr >= STARK_LAB_I2C_ADDR_MIN && addr <= STARK_LAB_I2C_ADDR_MAX &&
           (reg16 || reg <= 0xFFu) && stark_lab_i2c_hz_ok(hz);
}

stark_err_t lab_i2c_read(lab_i2c_t *c, uint8_t addr, uint16_t reg, bool reg16, uint8_t *buf,
                         size_t n, uint32_t hz)
{
    if (buf == NULL || n == 0u || n > STARK_LAB_I2C_R_MAX || !args_ok(addr, reg, reg16, hz)) {
        return STARK_ERR_INVALID_ARG;
    }
    uint8_t w[REG_BYTES_MAX];
    size_t wn = put_reg(w, reg, reg16);
    if (lab_i2c_refusal(c, addr, wn, n) != LAB_I2C_REFUSE_NONE) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    return c->io.xfer(c->io.ctx, addr, hz, w, wn, buf, n, LAB_I2C_XFER_MS);
}

/* Takes the arm: true when it was set for `fp` and is not stale. Consumed either way. */
static bool take_arm(lab_i2c_t *c, uint32_t fp)
{
    bool armed = c->armed;
    uint32_t age = c->io.now_ms(c->io.ctx) - c->armed_at_ms; /* unsigned: survives the wrap */
    c->armed = false;
    return armed && c->fp == fp && age <= LAB_I2C_ARM_MS;
}

stark_err_t lab_i2c_dump(lab_i2c_t *c, uint8_t addr, uint8_t *buf256, uint32_t hz)
{
    if (buf256 == NULL || !args_ok(addr, 0u, false, hz)) {
        c->armed = false;
        return STARK_ERR_INVALID_ARG;
    }
    uint8_t w[REG_BYTES_MAX];
    size_t wn = put_reg(w, 0u, false);
    if (lab_i2c_refusal(c, addr, wn, STARK_LAB_I2C_R_MAX) != LAB_I2C_REFUSE_NONE) {
        c->armed = false;
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (!take_arm(c, lab_i2c_fp_dump(addr))) {
        return STARK_ERR_STATE;
    }
    for (size_t row = 0; row < LAB_I2C_DUMP_ROWS; row++) {
        w[0] = (uint8_t)(row * STARK_LAB_I2C_R_MAX);
        stark_err_t err = c->io.xfer(c->io.ctx, addr, hz, w, wn, buf256 + row * STARK_LAB_I2C_R_MAX,
                                     STARK_LAB_I2C_R_MAX, LAB_I2C_XFER_MS);
        if (err != STARK_OK) {
            return err;
        }
    }
    return STARK_OK;
}

stark_err_t lab_i2c_write(lab_i2c_t *c, uint8_t addr, uint16_t reg, bool reg16, const uint8_t *v,
                          size_t n, uint32_t hz)
{
    if (v == NULL || n == 0u || n > LAB_I2C_WRITE_MAX || !args_ok(addr, reg, reg16, hz)) {
        c->armed = false;
        return STARK_ERR_INVALID_ARG;
    }
    uint8_t w[STARK_LAB_I2C_W_MAX];
    size_t wn = put_reg(w, reg, reg16);
    if (lab_i2c_refusal(c, addr, wn + n, 0u) != LAB_I2C_REFUSE_NONE) {
        c->armed = false;
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (!take_arm(c, lab_i2c_fp_write(addr, reg, reg16, v, n))) {
        return STARK_ERR_STATE;
    }
    memcpy(w + wn, v, n);
    return c->io.xfer(c->io.ctx, addr, hz, w, wn + n, NULL, 0u, LAB_I2C_XFER_MS);
}

void lab_i2c_arm(lab_i2c_t *c, uint32_t fingerprint)
{
    c->armed = true;
    c->fp = fingerprint;
    c->armed_at_ms = c->io.now_ms(c->io.ctx);
}

void lab_i2c_disarm(lab_i2c_t *c)
{
    c->armed = false;
}

bool lab_i2c_armed(const lab_i2c_t *c)
{
    return c->armed;
}

static uint32_t fnv(uint32_t h, uint8_t b)
{
    return (h ^ b) * 16777619u;
}

uint32_t lab_i2c_fp_write(uint8_t addr, uint16_t reg, bool reg16, const uint8_t *v, size_t n)
{
    uint32_t h = fnv(LAB_I2C_FP_SEED, (uint8_t)'W');
    h = fnv(h, addr);
    h = fnv(h, reg16 ? 1u : 0u);
    h = fnv(h, (uint8_t)(reg >> 8));
    h = fnv(h, (uint8_t)(reg & 0xFFu));
    h = fnv(h, (uint8_t)n);
    for (size_t i = 0; v != NULL && i < n; i++) {
        h = fnv(h, v[i]);
    }
    return h;
}

uint32_t lab_i2c_fp_dump(uint8_t addr)
{
    return fnv(fnv(LAB_I2C_FP_SEED, (uint8_t)'D'), addr);
}

static size_t hex_text(const uint8_t *b, size_t n, char *buf, size_t sz, bool spaced)
{
    static const char k_digits[] = "0123456789abcdef";
    size_t len = 0;
    if (sz == 0u) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        size_t need = (i > 0u && spaced ? 1u : 0u) + 2u;
        if (len + need + 1u > sz) {
            break;
        }
        if (i > 0u && spaced) {
            buf[len++] = ' ';
        }
        buf[len++] = k_digits[b[i] >> 4];
        buf[len++] = k_digits[b[i] & 0xFu];
    }
    buf[len] = '\0';
    return len;
}

size_t lab_i2c_hex(const uint8_t *b, size_t n, char *buf, size_t sz)
{
    return hex_text(b, n, buf, sz, true);
}

size_t lab_i2c_hex_packed(const uint8_t *b, size_t n, char *buf, size_t sz)
{
    return hex_text(b, n, buf, sz, false);
}

/* The service's pure helpers (stark_lab.h), under the names the apps call. */
uint32_t stark_lab_i2c_write_fingerprint(uint8_t addr, uint16_t reg, bool reg16, const uint8_t *v,
                                         size_t n)
{
    return lab_i2c_fp_write(addr, reg, reg16, v, n);
}

uint32_t stark_lab_i2c_dump_fingerprint(uint8_t addr)
{
    return lab_i2c_fp_dump(addr);
}

size_t stark_lab_i2c_hex(const uint8_t *b, size_t n, char *buf, size_t sz)
{
    return lab_i2c_hex(b, n, buf, sz);
}

size_t stark_lab_i2c_hex_packed(const uint8_t *b, size_t n, char *buf, size_t sz)
{
    return lab_i2c_hex_packed(b, n, buf, sz);
}
