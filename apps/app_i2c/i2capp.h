/*
 * i2capp.h — private to the I2C app (TASKS.md STARK-0704, ADR-0023 §5): the rows of the register
 * editors and the dump, and the text they show, built from the editor's configuration and what
 * stark_lab returned. i2c_views.c holds no service call and no UI call (pure, host-tested to every
 * line); app_i2c.c drives the service and the screen.
 *
 * The editor is a list of rows (a menu cannot take UP and DOWN for a value): each byte is two
 * nibbles, the Nibble row says which one LEFT and RIGHT change (high or low), and a nibble wraps
 * inside itself, so every byte is at most 15 steps from any other.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_lab.h"

#define I2CAPP_LEN_MAX    16u /* bytes of a register read */
#define I2CAPP_WRITE_MAX  8u  /* bytes of a register write */
#define I2CAPP_ROWS_MAX   16u /* the longest editor: the write one with eight bytes */
#define I2CAPP_RESULT_MAX 2u  /* result rows of a read: eight bytes each */
#define I2CAPP_DUMP_ROWS  16u /* a dump: 16 rows of 16 bytes */

typedef enum { I2CAPP_EDIT_READ, I2CAPP_EDIT_WRITE } i2capp_edit_t;

typedef struct {
    uint16_t reg;
    bool reg16;
    bool low_nibble; /* LEFT/RIGHT change the low nibble (else the high one) */
    uint8_t len;     /* a read's length, 1..16 */
    uint8_t count;   /* a write's byte count, 1..8 */
    uint8_t data[I2CAPP_WRITE_MAX];
} i2capp_cfg_t;

typedef enum {
    I2CROW_REG,    /* the register, or its low byte when 16-bit */
    I2CROW_REG_HI, /* the high byte of a 16-bit register */
    I2CROW_WIDTH,  /* 8-bit or 16-bit register */
    I2CROW_NIBBLE, /* which nibble LEFT/RIGHT change */
    I2CROW_LEN,    /* a read's length */
    I2CROW_COUNT,  /* a write's byte count */
    I2CROW_BYTE,   /* byte `idx` of a write */
    I2CROW_READ,   /* action: read */
    I2CROW_WRITE,  /* action: write (asks first) */
    I2CROW_NOTE,   /* a dimmed reminder, never selected */
    I2CROW_RESULT  /* bytes `idx * 8` of the read's result (dimmed, never selected) */
} i2cview_kind_t;

typedef struct {
    i2cview_kind_t kind;
    uint8_t idx;
} i2cview_row_t;

void i2cview_cfg_reset(i2capp_cfg_t *c); /* register 0, 8-bit, low nibble, one byte, zeros */

/* The rows of an editor, top to bottom; `result_len` is how many bytes the last read returned (0:
 * none yet), shown as rows of eight below the action. Returns how many (at most I2CAPP_ROWS_MAX).
 */
size_t i2cview_rows(i2capp_edit_t edit, const i2capp_cfg_t *c, size_t result_len,
                    i2cview_row_t *out);

/* The dimmed reminder under an editor's action row. */
const char *i2cview_note(i2capp_edit_t edit);

/* A short text for what a service call returned: "no answer", "timeout", "refused", ... */
const char *i2cview_error_text(stark_err_t err);

/* The label of a row: a constant, never NULL. */
const char *i2cview_label(const i2cview_row_t *row);

/*
 * The value column: written into `buf` (or a constant), NULL for a row with none. `result` holds
 * `result_len` bytes; `status` is the text beside the action row ("" for none, "ok", "no
 * answer", ...).
 */
const char *i2cview_value(const i2cview_row_t *row, const i2capp_cfg_t *c, const uint8_t *result,
                          size_t result_len, const char *status, char *buf, size_t n);

/* LEFT (-1) / RIGHT (+1) on a row: true when the configuration changed. Action, note and result
 * rows do not adjust; the length and count clamp; a nibble wraps. */
bool i2cview_adjust(i2capp_cfg_t *c, const i2cview_row_t *row, int dir);

/* "100 kHz" or "400 kHz"; the other speed for a step (anything else becomes 100 kHz). */
const char *i2cview_speed_text(uint32_t hz);
uint32_t i2cview_speed_step(uint32_t hz);

/* "0x23": a device row. */
void i2cview_addr_text(uint8_t addr, char *buf, size_t n);

/* "00".."f0" and the 16 bytes at that offset as 32 hex digits: a dump row. */
void i2cview_dump_label(size_t row, char *buf, size_t n);
void i2cview_dump_value(const uint8_t *dump, size_t row, char *buf, size_t n);

/* The confirmation texts (at most 159 bytes): the write's names address, register and bytes. */
void i2cview_write_message(uint8_t addr, const i2capp_cfg_t *c, char *buf, size_t n);
void i2cview_dump_message(uint8_t addr, char *buf, size_t n);

/* "0x6b" or "0x006b": the register as the logs and the rows show it. */
void i2cview_reg_text(uint16_t reg, bool reg16, char *buf, size_t n);
