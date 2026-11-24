/*
 * ui_port_i2c.c — the host UI port's lab service, I2C group (ui_port_i2c.h): stark_lab.h's
 * stark_lab_i2c_*() over lab_i2c.c and the fake bus. It mirrors stark_lab.c (components/stark_lab),
 * which adds the lock and the real clock; the decisions and the log lines are the same.
 */
#include "ui_port_i2c.h"
#include <stdio.h>
#include <string.h>
#include "fake_24c02.h"
#include "fake_lab.h"
#include "lab_i2c.h"
#include "stark_log.h"
#include "stark_module.h"

/* HARDWARE.md §2's map as far as the I2C rules read it: the two module ID EEPROM addresses. */
static const stark_board_pins_t k_map = {
    .lab_uart_tx = 38,
    .lab_uart_rx = 39,
    .module = {{.cs = 40, .irq0 = 41, .irq1 = 42, .en = -1, .eeprom_addr = 0x50},
               {.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1, .eeprom_addr = 0x51}},
};

static lab_i2c_t s_i2c;
static bool s_nfc;
static bool s_crowd;
static uint32_t s_now_ms;

const stark_board_pins_t *stark_board_pins(void)
{
    return &k_map;
}

uint32_t stark_module_caps(void)
{
    return s_nfc ? STARK_CAP_NFC : 0u;
}

static stark_err_t ext_probe(uint8_t addr)
{
    if (addr == STARK_LAB_I2C_PN532) {
        return s_nfc ? STARK_OK : STARK_ERR_NOT_FOUND;
    }
    return s_crowd ? STARK_OK : fake_24c02_probe(addr);
}

static stark_err_t ext_xfer(uint8_t addr, const uint8_t *w, size_t wn, uint8_t *r, size_t rn)
{
    (void)w;
    if (addr == STARK_LAB_I2C_PN532) {
        if (!s_nfc) {
            return STARK_ERR_NOT_FOUND;
        }
        if (rn > 0u) {
            memset(r, 0, rn);
        }
        return STARK_OK;
    }
    if (wn == 1u && rn > 0u) {
        return fake_24c02_read_at(addr, w[0], r, rn);
    }
    return STARK_ERR_NOT_SUPPORTED;
}

static stark_err_t io_probe(void *ctx, uint8_t addr, uint32_t timeout_ms)
{
    (void)ctx;
    return stark_lab_transport_i2c_probe(addr, timeout_ms);
}

static stark_err_t io_xfer(void *ctx, uint8_t addr, uint32_t hz, const uint8_t *w, size_t wn,
                           uint8_t *r, size_t rn, uint32_t timeout_ms)
{
    (void)ctx;
    return stark_lab_transport_i2c_xfer(addr, hz, w, wn, r, rn, timeout_ms);
}

static bool io_nfc(void *ctx)
{
    (void)ctx;
    return (stark_module_caps() & STARK_CAP_NFC) != 0u;
}

static uint32_t io_now(void *ctx)
{
    (void)ctx;
    return s_now_ms;
}

void ui_port_i2c_reset(void)
{
    fake_lab_reset();
    fake_24c02_reset();
    fake_lab_i2c_ext(ext_probe, ext_xfer);
    s_nfc = false;
    s_crowd = false;
    s_now_ms = 0;
    const lab_i2c_io_t io = {
        .probe = io_probe, .xfer = io_xfer, .nfc_confirmed = io_nfc, .now_ms = io_now};
    lab_i2c_init(&s_i2c, &io, &k_map);
}

void ui_port_i2c_eeprom(uint8_t addr, bool present)
{
    if (present) {
        uint8_t image[256];
        memset(image, 0xA5, sizeof image);
        fake_24c02_put(addr, image, sizeof image);
    } else {
        fake_24c02_remove(addr);
    }
}

void ui_port_i2c_nfc(bool confirmed)
{
    s_nfc = confirmed;
}

void ui_port_i2c_advance_ms(uint32_t ms)
{
    s_now_ms += ms;
}

unsigned ui_port_i2c_bus_calls(void)
{
    return fake_lab_i2c_calls();
}

/* ---- stark_lab.h ---------------------------------------------------------------------------- */

static void scan_text(const stark_lab_scan_t *r, char *buf, size_t sz)
{
    size_t used = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < r->n && i < 16u; i++) {
        used += (size_t)snprintf(buf + used, sz - used, "%s0x%02x", i > 0u ? " " : "", r->addr[i]);
    }
    if (r->n > 16u) {
        used += (size_t)snprintf(buf + used, sz - used, " +%u", (unsigned)(r->n - 16u));
    }
    if (r->n == 0u) {
        (void)snprintf(buf, sz, "none");
    }
}

stark_err_t stark_lab_i2c_scan(stark_lab_scan_t *out)
{
    if (out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    stark_err_t err = lab_i2c_scan(&s_i2c, out);
    char text[16u * 5u + 16u];
    scan_text(out, text, sizeof text);
    if (err == STARK_OK) {
        if (out->errors > 0u) {
            STARK_LOGI("lab", "i2c scan found %s (%u errors)", text, (unsigned)out->errors);
        } else {
            STARK_LOGI("lab", "i2c scan found %s", text);
        }
    } else {
        STARK_LOGW("lab", "i2c scan bus error found %s", text);
    }
    return err;
}

stark_err_t stark_lab_i2c_read(uint8_t addr, uint16_t reg, bool reg16, uint8_t *buf, size_t n,
                               uint32_t hz)
{
    stark_err_t err = lab_i2c_read(&s_i2c, addr, reg, reg16, buf, n, hz);
    if (err == STARK_OK) {
        char data[3u * STARK_LAB_I2C_R_MAX];
        (void)lab_i2c_hex(buf, n, data, sizeof data);
        STARK_LOGI("lab", "i2c read 0x%02x reg 0x%0*x %s", addr, reg16 ? 4 : 2, reg, data);
    } else if (err == STARK_ERR_NOT_SUPPORTED) {
        STARK_LOGW("lab", "i2c read refused: %s",
                   lab_i2c_refusal_text(lab_i2c_refusal(&s_i2c, addr, reg16 ? 2u : 1u, 1u)));
    } else {
        STARK_LOGW("lab", "i2c read 0x%02x reg 0x%0*x %s", addr, reg16 ? 4 : 2, reg,
                   stark_err_str(err));
    }
    return err;
}

stark_err_t stark_lab_i2c_dump(uint8_t addr, uint8_t *buf256, uint32_t hz)
{
    stark_err_t err = lab_i2c_dump(&s_i2c, addr, buf256, hz);
    if (err == STARK_ERR_NOT_SUPPORTED) {
        STARK_LOGW("lab", "i2c dump refused: %s",
                   lab_i2c_refusal_text(lab_i2c_refusal(&s_i2c, addr, 1u, 1u)));
    } else {
        STARK_LOGI("lab", "i2c dump 0x%02x %s", addr, err == STARK_OK ? "ok" : stark_err_str(err));
    }
    return err;
}

stark_err_t stark_lab_i2c_write(uint8_t addr, uint16_t reg, bool reg16, const uint8_t *v, size_t n,
                                uint32_t hz)
{
    stark_err_t err = lab_i2c_write(&s_i2c, addr, reg, reg16, v, n, hz);
    if (err == STARK_ERR_NOT_SUPPORTED) {
        STARK_LOGW("lab", "i2c write refused: %s",
                   lab_i2c_refusal_text(lab_i2c_refusal(&s_i2c, addr, (reg16 ? 2u : 1u) + n, 0u)));
    } else {
        char data[3u * LAB_I2C_WRITE_MAX];
        (void)lab_i2c_hex(v, v != NULL && n <= LAB_I2C_WRITE_MAX ? n : 0u, data, sizeof data);
        STARK_LOGI("lab", "i2c write 0x%02x reg 0x%0*x %s %s", addr, reg16 ? 4 : 2, reg, data,
                   err == STARK_OK ? "ok" : stark_err_str(err));
    }
    return err;
}

stark_err_t stark_lab_i2c_arm(uint32_t fingerprint)
{
    lab_i2c_arm(&s_i2c, fingerprint);
    return STARK_OK;
}

void stark_lab_i2c_disarm(void)
{
    lab_i2c_disarm(&s_i2c);
}

const char *stark_lab_i2c_refusal(uint8_t addr, bool reg16, bool write)
{
    lab_i2c_refusal_t why = lab_i2c_refusal(&s_i2c, addr, reg16 ? 2u : 1u, write ? 0u : 1u);
    return why == LAB_I2C_REFUSE_NONE ? NULL : lab_i2c_refusal_text(why);
}

bool ui_port_i2c_armed(void)
{
    return lab_i2c_armed(&s_i2c);
}

void ui_port_i2c_crowd(bool on)
{
    s_crowd = on;
}
