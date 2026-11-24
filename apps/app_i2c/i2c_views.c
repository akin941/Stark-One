/*
 * i2c_views.c — the I2C app's editors and dump rows (i2capp.h, TASKS.md STARK-0704): which rows a
 * view shows, what each says and what LEFT/RIGHT does to it. Pure: no service call, no UI call, no
 * allocation; every string is written into the caller's buffer or is a constant.
 */
#include <stdio.h>
#include "i2capp.h"

#define RESULT_PER_ROW 8u

static const char *const k_byte_labels[I2CAPP_WRITE_MAX] = {"Byte 0", "Byte 1", "Byte 2", "Byte 3",
                                                            "Byte 4", "Byte 5", "Byte 6", "Byte 7"};
static const char *const k_result_labels[I2CAPP_RESULT_MAX] = {"Bytes 0-7", "Bytes 8-15"};

void i2cview_cfg_reset(i2capp_cfg_t *c)
{
    *c = (i2capp_cfg_t){.low_nibble = true, .len = 1u, .count = 1u};
}

size_t i2cview_rows(i2capp_edit_t edit, const i2capp_cfg_t *c, size_t result_len,
                    i2cview_row_t *out)
{
    size_t n = 0;
    out[n++] = (i2cview_row_t){I2CROW_REG, 0};
    if (c->reg16) {
        out[n++] = (i2cview_row_t){I2CROW_REG_HI, 0};
    }
    out[n++] = (i2cview_row_t){I2CROW_WIDTH, 0};
    out[n++] = (i2cview_row_t){I2CROW_NIBBLE, 0};
    if (edit == I2CAPP_EDIT_READ) {
        out[n++] = (i2cview_row_t){I2CROW_LEN, 0};
        out[n++] = (i2cview_row_t){I2CROW_READ, 0};
        out[n++] = (i2cview_row_t){I2CROW_NOTE, 0};
        size_t rows = (result_len + RESULT_PER_ROW - 1u) / RESULT_PER_ROW;
        for (size_t i = 0; i < rows && i < I2CAPP_RESULT_MAX; i++) {
            out[n++] = (i2cview_row_t){I2CROW_RESULT, (uint8_t)i};
        }
    } else {
        out[n++] = (i2cview_row_t){I2CROW_COUNT, 0};
        for (size_t i = 0; i < c->count && i < I2CAPP_WRITE_MAX; i++) {
            out[n++] = (i2cview_row_t){I2CROW_BYTE, (uint8_t)i};
        }
        out[n++] = (i2cview_row_t){I2CROW_WRITE, 0};
        out[n++] = (i2cview_row_t){I2CROW_NOTE, 0};
    }
    return n;
}

const char *i2cview_label(const i2cview_row_t *row)
{
    switch (row->kind) {
        case I2CROW_REG:
            return "Reg";
        case I2CROW_REG_HI:
            return "Reg high";
        case I2CROW_WIDTH:
            return "Register";
        case I2CROW_NIBBLE:
            return "Nibble";
        case I2CROW_LEN:
            return "Length";
        case I2CROW_COUNT:
            return "Bytes";
        case I2CROW_BYTE:
            return k_byte_labels[row->idx % I2CAPP_WRITE_MAX];
        case I2CROW_READ:
            return "Read";
        case I2CROW_WRITE:
            return "Write";
        case I2CROW_RESULT:
            return k_result_labels[row->idx % I2CAPP_RESULT_MAX];
        case I2CROW_NOTE:
        default:
            return "";
    }
}

const char *i2cview_note(i2capp_edit_t edit)
{
    return edit == I2CAPP_EDIT_READ ? "A read can change a device's state" : "Own hardware only";
}

const char *i2cview_error_text(stark_err_t err)
{
    if (err == STARK_ERR_NOT_FOUND) {
        return "no answer";
    }
    if (err == STARK_ERR_TIMEOUT) {
        return "timeout";
    }
    if (err == STARK_ERR_NOT_SUPPORTED) {
        return "refused";
    }
    if (err == STARK_ERR_INVALID_ARG) {
        return "bad value";
    }
    return err == STARK_ERR_STATE ? "not confirmed" : "bus error";
}

void i2cview_reg_text(uint16_t reg, bool reg16, char *buf, size_t n)
{
    if (reg16) {
        (void)snprintf(buf, n, "0x%04x", (unsigned)reg);
    } else {
        (void)snprintf(buf, n, "0x%02x", (unsigned)(reg & 0xFFu));
    }
}

const char *i2cview_value(const i2cview_row_t *row, const i2capp_cfg_t *c, const uint8_t *result,
                          size_t result_len, const char *status, char *buf, size_t n)
{
    switch (row->kind) {
        case I2CROW_REG:
            (void)snprintf(buf, n, "0x%02x", (unsigned)(c->reg & 0xFFu));
            return buf;
        case I2CROW_REG_HI:
            (void)snprintf(buf, n, "0x%02x", (unsigned)(c->reg >> 8));
            return buf;
        case I2CROW_WIDTH:
            return c->reg16 ? "16-bit" : "8-bit";
        case I2CROW_NIBBLE:
            return c->low_nibble ? "low" : "high";
        case I2CROW_LEN:
            (void)snprintf(buf, n, "%u", (unsigned)c->len);
            return buf;
        case I2CROW_COUNT:
            (void)snprintf(buf, n, "%u", (unsigned)c->count);
            return buf;
        case I2CROW_BYTE:
            (void)snprintf(buf, n, "0x%02x", (unsigned)c->data[row->idx % I2CAPP_WRITE_MAX]);
            return buf;
        case I2CROW_READ:
        case I2CROW_WRITE:
            return status[0] != '\0' ? status : NULL;
        case I2CROW_RESULT: {
            size_t from = (size_t)row->idx * RESULT_PER_ROW;
            size_t left = from < result_len ? result_len - from : 0u;
            (void)stark_lab_i2c_hex(result + from, left < RESULT_PER_ROW ? left : RESULT_PER_ROW,
                                    buf, n);
            return buf;
        }
        case I2CROW_NOTE:
        default:
            return NULL;
    }
}

/* One nibble of a byte moves by one, wrapping inside itself: the other nibble is not touched. */
static uint8_t nibble_step(uint8_t b, bool low, int dir)
{
    unsigned shift = low ? 0u : 4u;
    unsigned nib = (b >> shift) & 0xFu;
    nib = (nib + (dir > 0 ? 1u : 15u)) & 0xFu;
    return (uint8_t)((b & ~(0xFu << shift)) | (nib << shift));
}

static uint8_t clamp_step(uint8_t v, int dir, uint8_t max)
{
    if (dir > 0) {
        return v < max ? (uint8_t)(v + 1u) : v;
    }
    return v > 1u ? (uint8_t)(v - 1u) : v;
}

bool i2cview_adjust(i2capp_cfg_t *c, const i2cview_row_t *row, int dir)
{
    if (dir == 0) {
        return false;
    }
    switch (row->kind) {
        case I2CROW_REG:
            c->reg =
                (uint16_t)((c->reg & 0xFF00u) | nibble_step((uint8_t)c->reg, c->low_nibble, dir));
            return true;
        case I2CROW_REG_HI:
            c->reg = (uint16_t)((c->reg & 0x00FFu) |
                                (uint16_t)(nibble_step((uint8_t)(c->reg >> 8), c->low_nibble, dir)
                                           << 8));
            return true;
        case I2CROW_WIDTH:
            c->reg16 = !c->reg16;
            if (!c->reg16) {
                c->reg &= 0x00FFu; /* an 8-bit register has no high byte */
            }
            return true;
        case I2CROW_NIBBLE:
            c->low_nibble = !c->low_nibble;
            return true;
        case I2CROW_LEN: {
            uint8_t v = clamp_step(c->len, dir, (uint8_t)I2CAPP_LEN_MAX);
            bool changed = v != c->len;
            c->len = v;
            return changed;
        }
        case I2CROW_COUNT: {
            uint8_t v = clamp_step(c->count, dir, (uint8_t)I2CAPP_WRITE_MAX);
            bool changed = v != c->count;
            c->count = v;
            return changed;
        }
        case I2CROW_BYTE: {
            uint8_t *b = &c->data[row->idx % I2CAPP_WRITE_MAX];
            *b = nibble_step(*b, c->low_nibble, dir);
            return true;
        }
        case I2CROW_READ:
        case I2CROW_WRITE:
        case I2CROW_NOTE:
        case I2CROW_RESULT:
        default:
            return false;
    }
}

const char *i2cview_speed_text(uint32_t hz)
{
    return hz == STARK_LAB_I2C_SPEED_400K ? "400 kHz" : "100 kHz";
}

uint32_t i2cview_speed_step(uint32_t hz)
{
    return hz == STARK_LAB_I2C_SPEED_400K ? STARK_LAB_I2C_SPEED_100K : STARK_LAB_I2C_SPEED_400K;
}

void i2cview_addr_text(uint8_t addr, char *buf, size_t n)
{
    (void)snprintf(buf, n, "0x%02x", (unsigned)addr);
}

void i2cview_dump_label(size_t row, char *buf, size_t n)
{
    (void)snprintf(buf, n, "%02x", (unsigned)((row * 16u) & 0xFFu));
}

void i2cview_dump_value(const uint8_t *dump, size_t row, char *buf, size_t n)
{
    (void)stark_lab_i2c_hex_packed(dump + (row % I2CAPP_DUMP_ROWS) * 16u, 16u, buf, n);
}

void i2cview_write_message(uint8_t addr, const i2capp_cfg_t *c, char *buf, size_t n)
{
    char reg[8];
    char data[3u * I2CAPP_WRITE_MAX];
    i2cview_reg_text(c->reg, c->reg16, reg, sizeof reg);
    (void)stark_lab_i2c_hex(c->data, c->count, data, sizeof data);
    (void)snprintf(buf, n, "Write 0x%02x reg %s: %s. Own hardware only.", (unsigned)addr, reg,
                   data);
}

void i2cview_dump_message(uint8_t addr, char *buf, size_t n)
{
    (void)snprintf(buf, n, "Dump 0x%02x: reads 256 bytes. A read can change a device's state.",
                   (unsigned)addr);
}
