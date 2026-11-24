/*
 * lab_i2c.h — the I2C scanner's rules (TASKS.md STARK-0704, ADR-0023 §5): the address-only sweep,
 * the register read, dump and write, the refusals, the arm tied to a fingerprint of what the user
 * confirmed, and the hex text. Pure, over an injected bus: no lock, no allocation, no ESP-IDF
 * header, every loop bounded (112 probes, 16 chunks). stark_lab.c binds it to
 * stark_lab_transport, the log and the module table; the host tests bind it to the test-only bus
 * (test/fake/fake_lab.c).
 *
 * Nothing here knows a device: the hints table (lab_i2c_hints.c) is a lookup by address that
 * probes nothing, and the one address the rules name is the PN532's, which is the NFC driver's
 * (stark_lab_transport.h: STARK_LAB_I2C_PN532).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_board.h"
#include "stark_lab.h"
#include "stark_lab_transport.h"

#define LAB_I2C_PROBE_MS  5u     /* each address of the sweep */
#define LAB_I2C_XFER_MS   50u    /* a transaction of up to 16 bytes at 100 kHz takes 2 ms */
#define LAB_I2C_ERR_RUN   3u     /* errors in a row that end the sweep */
#define LAB_I2C_DUMP_ROWS 16u    /* a dump is 16 transactions of 16 bytes */
#define LAB_I2C_WRITE_MAX 8u     /* bytes after the register */
#define LAB_I2C_ARM_MS    30000u /* an arm is stale after this long */

_Static_assert(STARK_LAB_I2C_SPEED_100K == STARK_LAB_I2C_HZ_STD &&
                   STARK_LAB_I2C_SPEED_400K == STARK_LAB_I2C_HZ_FAST,
               "the app's speeds are the transport's");

/* Why a transaction is refused: the text the log and the app show. */
typedef enum {
    LAB_I2C_REFUSE_NONE,
    LAB_I2C_REFUSE_EEPROM, /* "module eeprom": a module ID EEPROM is read, never written */
    LAB_I2C_REFUSE_NFC     /* "nfc reader": the PN532 belongs to the NFC driver while confirmed */
} lab_i2c_refusal_t;

typedef struct {
    void *ctx;
    stark_err_t (*probe)(void *ctx, uint8_t addr, uint32_t timeout_ms);
    stark_err_t (*xfer)(void *ctx, uint8_t addr, uint32_t hz, const uint8_t *w, size_t wn,
                        uint8_t *r, size_t rn, uint32_t timeout_ms);
    bool (*nfc_confirmed)(void *ctx);
    uint32_t (*now_ms)(void *ctx);
} lab_i2c_io_t;

typedef struct {
    lab_i2c_io_t io;
    uint8_t eeprom[STARK_BOARD_MODULE_SLOTS]; /* the board's module ID EEPROM addresses */
    bool armed;
    uint32_t fp;
    uint32_t armed_at_ms;
} lab_i2c_t;

/* The EEPROM addresses come from the pin map (stark_board.h), never from here. */
void lab_i2c_init(lab_i2c_t *c, const lab_i2c_io_t *io, const stark_board_pins_t *pins);

/*
 * The sweep: one probe per address 0x08 to 0x77, 5 ms each, in order. A NACK is an empty address;
 * a timeout or a bus error counts, and LAB_I2C_ERR_RUN of them in a row end the sweep with
 * STARK_ERR_IO and the list so far (a NACK or an ACK between them starts the count again). The
 * result's `errors` is how many probes failed. Always 112 probes at most, whatever the bus does.
 */
stark_err_t lab_i2c_scan(lab_i2c_t *c, stark_lab_scan_t *out);

/*
 * Whether a transaction of `wn` bytes written and `rn` read is refused for this address, and why:
 * a module ID EEPROM takes a one-byte pointer and a read only (a write, or more than a pointer,
 * is refused); the PN532 takes nothing while NFC is confirmed. The same rule as the transport's
 * guard (stark_lab_i2c_allowed()), with the reason.
 */
lab_i2c_refusal_t lab_i2c_refusal(const lab_i2c_t *c, uint8_t addr, size_t wn, size_t rn);

/* "module eeprom", "nfc reader", or "" for LAB_I2C_REFUSE_NONE. */
const char *lab_i2c_refusal_text(lab_i2c_refusal_t why);

/*
 * Register read, 1 to 16 bytes: the register is sent as one byte, or two (high first) when `reg16`,
 * and `n` bytes follow a repeated start. STARK_ERR_INVALID_ARG for an address outside 0x08 to 0x77,
 * n outside 1..16, a register above 0xFF without `reg16`, or a speed that is not 100 or 400 kHz;
 * STARK_ERR_NOT_SUPPORTED when refused (a 16-bit register on a module EEPROM is a write, the
 * PN532 while NFC is confirmed); the bus's error otherwise. No arm: a read is not a write.
 */
stark_err_t lab_i2c_read(lab_i2c_t *c, uint8_t addr, uint16_t reg, bool reg16, uint8_t *buf,
                         size_t n, uint32_t hz);

/*
 * 256 bytes from register 0, one byte registers, in 16 transactions of 16: STARK_ERR_STATE
 * unless armed for lab_i2c_fp_dump(addr) (the arm is consumed by the call, whatever it returns).
 * Stops at the first transaction that fails. Arguments and refusals as for lab_i2c_read().
 */
stark_err_t lab_i2c_dump(lab_i2c_t *c, uint8_t addr, uint8_t *buf256, uint32_t hz);

/*
 * Register write, 1 to 8 bytes, one write transaction (register, then the bytes): STARK_ERR_STATE
 * unless armed for lab_i2c_fp_write() of exactly these arguments, STARK_ERR_NOT_SUPPORTED for a
 * module ID EEPROM or the PN532 while confirmed (checked before the arm, so a refusal needs none).
 * The arm is consumed by the call, whatever it returns. A different payload, or an arm older than
 * LAB_I2C_ARM_MS, is STARK_ERR_STATE. Arguments as for lab_i2c_read() with n 1..8.
 */
stark_err_t lab_i2c_write(lab_i2c_t *c, uint8_t addr, uint16_t reg, bool reg16, const uint8_t *v,
                          size_t n, uint32_t hz);

/* The arm: what the user confirmed, as a fingerprint. Replaces an earlier arm. */
void lab_i2c_arm(lab_i2c_t *c, uint32_t fingerprint);
void lab_i2c_disarm(lab_i2c_t *c);

/* True after an arm that a call has not consumed yet (stale ones count until a call or disarm). */
bool lab_i2c_armed(const lab_i2c_t *c);

#define LAB_I2C_FP_SEED 2166136261u /* 32-bit FNV-1a */

/*
 * The fingerprint of a dialog's content: 32-bit FNV-1a over 'W', address, `reg16` (0 or 1), the
 * register high and low byte, the count and the bytes; and over 'D' and the address for a dump.
 * Speed is not part of it: the dialog does not show it.
 */
uint32_t lab_i2c_fp_write(uint8_t addr, uint16_t reg, bool reg16, const uint8_t *v, size_t n);
uint32_t lab_i2c_fp_dump(uint8_t addr);

/* "01 ab ff": lower-case, space separated, whole bytes that fit; returns the length (0 if sz is
 * 0). */
size_t lab_i2c_hex(const uint8_t *b, size_t n, char *buf, size_t sz);

/* "01abff": the same without separators. */
size_t lab_i2c_hex_packed(const uint8_t *b, size_t n, char *buf, size_t sz);

/* The hints table (lab_i2c_hints.c; the lookup is stark_lab_i2c_hint() in stark_lab.h). */
#define LAB_I2C_HINTS_MAX 48u
size_t lab_i2c_hints_count(void);
uint8_t lab_i2c_hints_addr(size_t i); /* 0 past the end */
