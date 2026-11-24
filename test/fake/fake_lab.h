/*
 * fake_lab.h — the test-only model of the lab's wires (TASKS.md STARK-0701, ADR-0023 §2, §9).
 * Pads (hi-Z, input with a pull, output with a level), each with an external source the
 * harness sets and wires between pads, and the shared I2C bus with five known devices
 * (STARK-0704), the UART peer (STARK-0705) and the SPI devices on a module slot (STARK-0706).
 * Spec-derived, never a recording of a device.
 *
 * It sits behind stark_lab_transport: the host tests bind the pure lab core to it directly,
 * and the emulator test image's substituted transport (test/emu/storage_image/components/
 * stark_lab_transport) calls it from the pokes. It knows no pin policy: that is stark_board's
 * and the transport's guard. Not thread-safe: one caller at a time, like the transport.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_err.h"

#define FAKE_LAB_PINS 49 /* GPIO 0..48 */

typedef enum {
    FAKE_LAB_EXT_NONE,  /* nothing external drives the pad */
    FAKE_LAB_EXT_HIGH,  /* an external source drives it high */
    FAKE_LAB_EXT_LOW,   /* ... low */
    FAKE_LAB_EXT_SQUARE /* a square wave of hz and duty, evaluated at fake_lab_time_us() */
} fake_lab_ext_t;

typedef enum {
    FAKE_LAB_FAULT_NONE,
    FAKE_LAB_FAULT_PAD_STUCK_LOW,  /* a read of the pad returns low whatever drives it */
    FAKE_LAB_FAULT_PAD_STUCK_HIGH, /* ... high */
    FAKE_LAB_FAULT_CONFIG_ERROR,   /* pad_in, pad_out and pwm_start fail with STARK_ERR_IO */
    FAKE_LAB_FAULT_SPI_ERROR,      /* an SPI transfer fails with STARK_ERR_IO (STARK-0706) */
    FAKE_LAB_FAULT_SPI_NO_ANSWER,  /* ... or the device does not answer: MISO reads all 0xFF */
    FAKE_LAB_FAULT_COUNT
} fake_lab_fault_t;

typedef enum {
    FAKE_LAB_PAD_HIZ,
    FAKE_LAB_PAD_IN,
    FAKE_LAB_PAD_OUT,
    FAKE_LAB_PAD_PWM /* a square wave of hz and duty, evaluated at fake_lab_time_us() */
} fake_lab_mode_t;

/* Everything back to the power-on state: all pads hi-Z, no source, no wire, no fault. */
void fake_lab_reset(void);

/* The clock the square wave is evaluated at (µs); it never advances on its own. */
void fake_lab_time_us(uint64_t now_us);

/* An external source on a pad. hz and duty_pct (1..99) matter for FAKE_LAB_EXT_SQUARE only. */
void fake_lab_ext(int pin, fake_lab_ext_t mode, uint32_t hz, uint8_t duty_pct);

/* Pad `from`'s output becomes the external source of pad `to` while `from` is an output.
 * from < 0 removes every wire. At most four; extras are ignored. */
void fake_lab_wire(int from, int to);

/* The next `count` matching operations fail (-1: until cleared, 0: clear). pin -1: any pad. */
void fake_lab_fault(fake_lab_fault_t fault, int pin, int count);

/* The pad calls of stark_lab_transport.h, without its guard. STARK_ERR_INVALID_ARG outside 0..48.
 */
stark_err_t fake_lab_pad_in(int pin, uint8_t pull); /* 0 none, 1 up, 2 down */
stark_err_t fake_lab_pad_out(int pin, bool level);
stark_err_t fake_lab_pad_read(int pin, bool *level);
stark_err_t fake_lab_pad_hiz(int pin);

/*
 * The PWM and pulse calls of stark_lab_transport.h. A pad in PWM mode drives a square wave on
 * the pad and on any pad it is wired to; hz and duty are kept as given. fake_lab_pulse() leaves
 * the pad an output at the opposite level and counts the pulse (the fake has no waiting to do);
 * its width is recorded for the tests. A pulse is refused like a drive by FAULT_CONFIG_ERROR.
 */
stark_err_t fake_lab_pwm_start(int pin, uint32_t hz, uint8_t duty_pct);
stark_err_t fake_lab_pulse(int pin, bool level, uint32_t us);

/*
 * Edge capture (STARK-0703): the stamps of the edges of whatever drives the pad over the next
 * `window_us` from fake_lab_time_us(), at FAKE_LAB_TICK_HZ (240 MHz, 240 ticks to the
 * microsecond), the way stark_hal_gpio_edges() returns them: the first `cap` are stored in `ts`,
 * `*seen` counts edges up to cap + 1 (FAKE_LAB_GATE_LIMIT when cap is 0), `*first_level` is the
 * level before the first one. A steady source gives no edge. The wave is anchored at tick 0 and
 * its period is FAKE_LAB_TICK_HZ / hz in whole ticks, so the frequency it models is that of
 * a square wave of that period, within hz / 240 MHz. The clock does not advance (the fake never
 * waits); a fault applies as for a read (stuck: no edge, the stuck level; config error: IO).
 * The pad is not changed.
 */
#define FAKE_LAB_TICK_HZ      240000000u
#define FAKE_LAB_TICKS_PER_US 240u
#define FAKE_LAB_GATE_LIMIT   4096u
stark_err_t fake_lab_pad_edges(int pin, uint32_t window_us, uint32_t *ts, size_t cap, size_t *n,
                               uint32_t *seen, bool *first_level);

/* Imperfections for the capture, off at the reset: every edge displaced by up to +-jitter_ticks
 * (a deterministic function of the seed and the edge's number, kept in order), a pair of 0.5 us
 * edges after every `every_edges`-th edge that is followed by room for it (0: none), and an
 * offset added to every stamp (to put a window across the 32-bit wrap of the counter). */
void fake_lab_edge_noise(uint32_t jitter_ticks, uint32_t seed);
void fake_lab_edge_glitches(uint32_t every_edges);
void fake_lab_edge_epoch(uint32_t ticks);

/* The square wave at `now_us` (the fake's own formula, shared with the external source). */
bool fake_lab_square(uint32_t hz, uint8_t duty_pct, uint64_t now_us);

/* Inspection for tests: how many pad calls reached a valid pad, and what a pad is set to. */
unsigned fake_lab_calls(void);
fake_lab_mode_t fake_lab_mode(int pin);
uint8_t fake_lab_pull(int pin);
bool fake_lab_out_level(int pin);
uint32_t fake_lab_pwm_hz(int pin);
uint8_t fake_lab_pwm_duty(int pin);
unsigned fake_lab_pulses(void);        /* pulses done since the reset */
uint32_t fake_lab_last_pulse_us(void); /* the width of the latest one */
bool fake_lab_last_pulse_level(void);

/* ---- the I2C bus (STARK-0704, ADR-0023 §5) -------------------------------------------------
 *
 * Five spec-derived devices at known addresses, chosen by a bitmask (bit i, the order below):
 *   0x23  light sensor class: a read returns the two bytes 0x01 0x23, then zeros; writes are
 *         commands that it accepts and ignores
 *   0x3C  display class: acks, reads zero, accepts writes and ignores them
 *   0x48  four 16-bit registers (pointer 0..3): 0 is read-only (0x1900), 1 holds 0x60A0, 2 holds
 *         0x4B00, 3 holds 0x5000; a write is the pointer then pairs of bytes, high first
 *   0x68  a 256-register file, WHO_AM_I at 0x75 = 0x68 (read-only), 0x6B = 0x40, the rest zero and
 *         writable; the first byte written is the pointer, the following bytes are stored from it,
 *         and a read continues from the pointer, both auto-incrementing (and wrapping at 256)
 *   0x76  the same file shape with 0xD0 = 0x60 (read-only)
 * Any other address is NACKed unless the harness binds the module EEPROMs and the PN532 in
 * (fake_lab_i2c_ext()), so one scan shows one consistent bus. It knows no policy: the transport's
 * guard and the service's rules are the policy.
 */
#define FAKE_LAB_I2C_DEVICES 5
#define FAKE_LAB_I2C_ALL     0x1Fu

typedef enum {
    FAKE_LAB_I2C_FAULT_NONE,
    FAKE_LAB_I2C_NACK_ALL,    /* every probe and transaction is NACKed */
    FAKE_LAB_I2C_TIMEOUT,     /* the next `count` transactions at `addr` (-1: any) time out */
    FAKE_LAB_I2C_STUCK,       /* every call times out */
    FAKE_LAB_I2C_DROP_DEVICE, /* the device at `addr` answers once and then vanishes */
    FAKE_LAB_I2C_FAULT_COUNT
} fake_lab_i2c_fault_t;

/* What answers at the addresses the five devices do not use (the module EEPROMs, the PN532). */
typedef stark_err_t (*fake_lab_i2c_probe_fn)(uint8_t addr);
typedef stark_err_t (*fake_lab_i2c_xfer_fn)(uint8_t addr, const uint8_t *w, size_t wn, uint8_t *r,
                                            size_t rn);
void fake_lab_i2c_ext(fake_lab_i2c_probe_fn probe, fake_lab_i2c_xfer_fn xfer);

/* The devices on the bus, as a bitmask over the five (the reset: all of them). */
void fake_lab_i2c_devices(uint8_t mask);

/* `count` as for fake_lab_fault() (-1: until cleared, 0: clear); `addr` for TIMEOUT and
 * DROP_DEVICE. */
void fake_lab_i2c_fault(fake_lab_i2c_fault_t kind, int addr, int count);

/* An address-only check: STARK_OK, or STARK_ERR_NOT_FOUND (NACK) / STARK_ERR_TIMEOUT. */
stark_err_t fake_lab_i2c_probe(uint8_t addr);

/* A write of `wn` bytes then a read of `rn` after a repeated start (either may be zero), as the
 * transport's i2c_xfer(). STARK_ERR_NOT_FOUND for an address that does not answer. */
stark_err_t fake_lab_i2c_xfer(uint8_t addr, const uint8_t *w, size_t wn, uint8_t *r, size_t rn);

/* Inspection for tests: bus calls since the reset, write transactions that carried data bytes to a
 * device of the five, a stored register and the 0x48 registers. */
unsigned fake_lab_i2c_calls(void);
unsigned fake_lab_i2c_writes(void);
uint8_t fake_lab_i2c_reg(uint8_t addr, uint8_t reg); /* 0x68 and 0x76 only; 0 otherwise */
uint16_t fake_lab_i2c_word(uint8_t reg);             /* 0x48, reg 0..3 */

/* ---- the UART peer (STARK-0705, ADR-0023 §7) -----------------------------------------------
 *
 * What is on the other end of the lab UART, selected by a mode, in model time (fake_lab_time_us():
 * the fake never waits, so a character that has not finished crossing the wire by "now" has not
 * arrived). The device's own settings come from fake_lab_uart_open(); the peer has settings of its
 * own (fake_lab_uart_peer()):
 *   SILENT  nothing answers and nothing talks
 *   LOOP    TX is wired to RX: every character comes back when it has crossed the wire, at the
 *           device's own settings, so it always matches
 *   ECHO    a peer that sends every character back, at its own baud and format: a baud more than
 *           3 % off or a different format garbles it in both directions (a deterministic bijection)
 *           and counts a framing error (a parity error when the receiver checks parity)
 *   TEXT    the peer talks, unasked: 50 lines "stark uart scene line NN\r\n" (1 300 bytes), once
 * from the moment the device opens, then silence; BINARY the same with 1 024 bytes (j * 7 + 3) &
 * 0xFF. The same peer is what auto-baud sees on the RX pad: the scene again and again from the
 * start of a capture (fake_lab_pad_edges() on the RX pin), its edges built bit by bit at the peer's
 * baud, with the jitter configured for the edge capture. The receiver's buffer is the one the
 * device opened with: a stream that outruns it loses its oldest characters and counts an overrun
 * (the model's choice; the hardware loses the newest). Nothing here knows a pin policy: the
 * transport's guard is the policy.
 */
typedef enum {
    FAKE_LAB_UART_SILENT,
    FAKE_LAB_UART_LOOP,
    FAKE_LAB_UART_ECHO,
    FAKE_LAB_UART_TEXT,
    FAKE_LAB_UART_BINARY,
    FAKE_LAB_UART_MODES
} fake_lab_uart_mode_t;

/* Which pads are the lab UART's (the RX pad is where the stream's edges appear); -1 for none. The
 * reset forgets them. */
void fake_lab_uart_pins(int tx, int rx);

/* The peer's mode and, for ECHO, TEXT and BINARY, its baud and format index (0 8N1, 1 8E1, 2 8O1,
 * 3 8N2, 4 7E1). A mode past the list or a format past it is ignored. */
void fake_lab_uart_peer(fake_lab_uart_mode_t mode, uint32_t baud, uint8_t fmt);

/* The device's side: `tx` routes TX (writes work only then). STARK_ERR_INVALID_ARG for a zero baud,
 * a format past 4 or a zero buffer, STARK_ERR_STATE when already open. Opening restarts the scene.
 */
stark_err_t fake_lab_uart_open(uint32_t baud, uint8_t fmt, size_t rx_buf, bool tx);
void fake_lab_uart_close(void);
/* A new device baud, the format and the characters in flight unchanged. STARK_ERR_STATE when not
 * open, STARK_ERR_INVALID_ARG for zero. */
stark_err_t fake_lab_uart_set_baud(uint32_t baud);
bool fake_lab_uart_is_open(void);
bool fake_lab_uart_tx(void);

/* Sends `n` characters at the device's settings: returns n, or 0 unless open with TX. The wire time
 * is fake_lab_uart_wire_us(): the caller advances the model clock by it if it means to wait. */
size_t fake_lab_uart_write(const uint8_t *buf, size_t n);
uint32_t fake_lab_uart_wire_us(size_t n);

/* The characters that have arrived by now, up to `max`; the errors that came with them are added to
 * the counters (any may be NULL). */
size_t fake_lab_uart_read(uint8_t *buf, size_t max, uint32_t *framing, uint32_t *parity,
                          uint32_t *overrun);

/* The scene of a TEXT or BINARY peer, for tests; the bits on the wire per character of a format
 * (0 past 4); characters dropped because more than 256 were in flight. */
size_t fake_lab_uart_scene_len(fake_lab_uart_mode_t mode);
uint8_t fake_lab_uart_scene_byte(fake_lab_uart_mode_t mode, size_t j);
uint32_t fake_lab_uart_bits(uint8_t fmt);
unsigned fake_lab_uart_dropped(void);

/* ---- the SPI devices (STARK-0706, ADR-0023 §6) ---------------------------------------------
 *
 * What answers on a module slot's chip select, chosen by a mode (the reset: NONE). One transfer is
 * a chip-select-framed exchange of n bytes, MISO as MOSI is clocked: NONE   nothing drives MISO: it
 * idles low and reads 0x00 whatever is sent FLASH  a JEDEC NOR flash class device: while the
 * command byte is clocked MISO reads 0x00; then 9F returns ef 40 18 (manufacturer, type, capacity)
 * and zeros after them, 05 returns the status register, 0x00, 03 with three address bytes streams
 * the counting pattern (the byte at address a is a & 0xFF, wrapping at 24 bits) and every other
 * command, writes and erases included, is accepted and ignored: the model has no state ECHO   MISO
 * is the previous MOSI byte (the first MISO byte is 0x00) FAKE_LAB_FAULT_SPI_ERROR makes the
 * transfer fail and FAKE_LAB_FAULT_SPI_NO_ANSWER makes MISO read all 0xFF (a missing device on a
 * pulled-up line); both are armed through fake_lab_fault() and count one use per transfer.
 * fake_lab_spi_ext() binds a chip the model does not know (the host tests bind the fake CC1101 to
 * slot 0 to prove that nothing reaches it); while bound it answers every transfer instead of the
 * mode. The model knows no policy: the transport's guard is the policy.
 */
typedef enum {
    FAKE_LAB_SPI_NONE,
    FAKE_LAB_SPI_FLASH,
    FAKE_LAB_SPI_ECHO,
    FAKE_LAB_SPI_MODES
} fake_lab_spi_mode_t;

typedef stark_err_t (*fake_lab_spi_ext_fn)(uint8_t slot, const uint8_t *tx, uint8_t *rx, size_t n);

void fake_lab_spi_device(fake_lab_spi_mode_t mode); /* a mode past the list is ignored */
void fake_lab_spi_ext(fake_lab_spi_ext_fn fn);      /* NULL unbinds */

/* One transfer of `n` bytes (1 to 32). STARK_ERR_INVALID_ARG for a NULL or a length outside that.
 */
stark_err_t fake_lab_spi_xfer(uint8_t slot, uint32_t hz, uint8_t mode, const uint8_t *tx,
                              uint8_t *rx, size_t n);

/* Inspection for tests: transfers that reached the model since the reset, and the latest one's
 * slot, clock, mode and length. */
unsigned fake_lab_spi_calls(void);
uint8_t fake_lab_spi_last_slot(void);
uint32_t fake_lab_spi_last_hz(void);
uint8_t fake_lab_spi_last_mode(void);
size_t fake_lab_spi_last_len(void);
