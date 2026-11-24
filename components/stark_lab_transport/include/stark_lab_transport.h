/*
 * stark_lab_transport.h — the lab tools' wires (TASKS.md STARK-0701, ADR-0023 §2): the one
 * seam between the lab service and the hardware, and the fifth and last substitution of the
 * emulator test image. Today it carries the pad group (configure, read, hi-Z), the PWM and
 * pulse outputs (STARK-0702), the edge capture (STARK-0703), the shared I2C bus (STARK-0704:
 * probe and one transaction at a time), the lab UART (STARK-0705: UART1 on the two lab pins) and
 * SPI on a leased module slot (STARK-0706: one bounded transaction).
 *
 * Every pad call re-checks the pin against stark_board's classification, whatever the
 * caller says (defence in depth, SECURITY_SCOPE §4.6): a pin that is not offered returns
 * STARK_ERR_NOT_SUPPORTED before any hardware is touched. The one call allowed on a pin
 * that is no longer offered is stark_lab_transport_pad_hiz(), which only releases it.
 * The emulator test image substitutes this component with one built on the test-only
 * fake pads (test/fake/fake_lab.c); the policy below is shared, so neither can touch what
 * it refuses.
 *
 * Callers are serialised by the lab service's mutex; a pad is one owner at a time.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_board.h"
#include "stark_err.h"
#include "stark_module.h"

#define STARK_LAB_PAD_NONE     0u
#define STARK_LAB_PAD_UP       1u
#define STARK_LAB_PAD_DOWN     2u

/* The PWM output (ADR-0023 §4): LEDC channel 2, one pad at a time. */
#define STARK_LAB_PWM_CHANNEL  2u
#define STARK_LAB_PWM_HZ_MIN   100u
#define STARK_LAB_PWM_HZ_MAX   40000u
#define STARK_LAB_PULSE_US_MIN 10u
#define STARK_LAB_PULSE_US_MAX 1000000u

/* Prepares the transport. Idempotent. */
stark_err_t stark_lab_transport_open(void);
void stark_lab_transport_close(void);

/* Input with the pull (STARK_LAB_PAD_*), interrupts off. STARK_ERR_STATE when closed. */
stark_err_t stark_lab_transport_pad_in(int pin, uint8_t pull);

/* Output at the weakest drive strength, the pad readable: weak drive, read-back enabled. */
stark_err_t stark_lab_transport_pad_out(int pin, bool level);

/* The level on the pad, an output's own read-back included. */
stark_err_t stark_lab_transport_pad_read(int pin, bool *level);

/* Input, no pull, interrupts off. Allowed on any pin the board gives a class (not RESERVED). */
stark_err_t stark_lab_transport_pad_hiz(int pin);

/*
 * PWM on a pad at `hz` (STARK_LAB_PWM_HZ_MIN..MAX) and `duty_pct` (0..100), weakest drive
 * strength, on LEDC channel STARK_LAB_PWM_CHANNEL only (the buzzer's and the backlight's channels
 * are never touched). STARK_ERR_INVALID_ARG outside those ranges. A failure leaves the pad in hi-Z.
 */
stark_err_t stark_lab_transport_pwm_start(int pin, uint32_t hz, uint8_t duty_pct);

/* Stops the PWM channel (idle level low) and returns the pad to hi-Z. Allowed on any classified
 * pin. */
stark_err_t stark_lab_transport_pwm_stop(int pin);

/*
 * One pulse: the pad is driven to `level` (weakest strength) for `us` microseconds
 * (STARK_LAB_PULSE_US_MIN..MAX, else STARK_ERR_INVALID_ARG), then to the opposite level, where it
 * stays: the caller restores the mode the pad had. Under 1 ms it is a busy wait, longer ones wait
 * in 10 ms slices at most (so stark_lab_transport_pulse_cancel() takes effect within one), and a
 * cancelled pulse still ends at the opposite level.
 */
stark_err_t stark_lab_transport_pulse(int pin, bool level, uint32_t us);

/* Ends a pulse that is waiting, early. Any task; no lock; a pulse that starts later is not
 * affected. */
void stark_lab_transport_pulse_cancel(void);

/*
 * Edge capture (STARK-0703, ADR-0023 §4): the any-edge interrupt of one offered pad is on for
 * `window_us` (STARK_LAB_EDGES_WINDOW_US_MIN..MAX, else STARK_ERR_INVALID_ARG) and every edge is
 * stamped with the CPU cycle counter into `ts` (the first `cap` are stored; cap 0 only counts).
 * `*n` is how many were stored and `*seen` how many edges came, counted up to cap + 1 (or
 * STARK_LAB_EDGES_GATE_MAX when cap is 0): the capture ends at once when that many have come, so
 * `*seen > cap` is a ring that overflowed. `*first_level` is the level before the first edge. The
 * pad is not configured: it stays what it was, an input with its pull, and the interrupt is
 * removed on every way out. A cancel ends the wait within 10 ms and returns STARK_ERR_STATE.
 * It is an input operation (an interrupt on a pad, never a drive); the service allows it only on
 * a pin it holds as an input. Re-checks the class like every pad call.
 */
#define STARK_LAB_EDGES_WINDOW_US_MIN 10000u
#define STARK_LAB_EDGES_WINDOW_US_MAX 4000000u
#define STARK_LAB_EDGES_GATE_MAX      4096u
stark_err_t stark_lab_transport_pad_edges(int pin, uint32_t window_us, uint32_t *ts, size_t cap,
                                          size_t *n, uint32_t *seen, bool *first_level);

/* Ends a capture that is waiting, early. Any task; no lock; a capture that starts later is not
 * affected. */
void stark_lab_transport_edges_cancel(void);

/* The clock the stamps count at, in hertz. */
uint32_t stark_lab_transport_tick_hz(void);

/*
 * The offer, as a pure rule: a LAB pin always, a SLOT pin only while its slot is empty
 * (reads ABSENT), a RESERVED pin never.
 */
static inline bool stark_lab_pin_offered(stark_pin_class_t cls, bool slot_absent)
{
    return cls == STARK_PIN_LAB || (cls == STARK_PIN_SLOT && slot_absent);
}

/* True when slot `slot` is listed in `list` and reads ABSENT (an unlisted slot: not absent). */
static inline bool stark_lab_slot_absent(const stark_module_entry_t *list, size_t n, int slot)
{
    for (size_t i = 0; i < n; i++) {
        if (list[i].kind == STARK_MODULE_SLOT && (int)list[i].slot == slot) {
            return list[i].state == STARK_MODULE_ABSENT;
        }
    }
    return false;
}

/*
 * The live offer for one GPIO number: stark_board's class and, for a slot's line, the module
 * table as it is now. Shared by the hardware transport and the test one. An empty table (the
 * module service has not started) offers no slot line. Any task.
 */
static inline bool stark_lab_pad_ok(int pin)
{
    int slot = -1;
    stark_pin_class_t cls = stark_board_pin_class(pin, &slot);
    if (cls != STARK_PIN_SLOT) {
        return stark_lab_pin_offered(cls, false);
    }
    stark_module_entry_t list[STARK_MODULE_ENTRIES];
    size_t n = stark_module_list(list, STARK_MODULE_ENTRIES);
    if (n > STARK_MODULE_ENTRIES) {
        n = STARK_MODULE_ENTRIES;
    }
    return stark_lab_pin_offered(cls, stark_lab_slot_absent(list, n, slot));
}

/* The release exemption: any pin the board classifies. */
static inline bool stark_lab_pad_hiz_ok(int pin)
{
    return stark_board_pin_class(pin, NULL) != STARK_PIN_RESERVED;
}

/* ---- the shared I2C bus (STARK-0704, ADR-0023 §5) ---------------------------------------------
 *
 * One probe or one transaction at a time on the board's I2C bus (stark_board_i2c_bus()), which
 * the NFC and module tasks share: the bus's own lock keeps a lab transaction from interleaving
 * with theirs, and a transaction attaches a device handle for itself and removes it again. The
 * call is refused, before any bus traffic, by the shared guard below: an address outside 0x08 to
 * 0x77, a write to a module ID EEPROM (ADR-0021 §2: a descriptor is never written; a read may
 * carry a one-byte pointer only), and any data transaction with the PN532 while NFC is confirmed
 * (its driver owns the protocol). The same guard is applied by the test image's transport and by
 * the host shim, so none can do what another refuses (defence in depth, SECURITY_SCOPE §4.6; the
 * service applies it first).
 */
#define STARK_LAB_I2C_ADDR_MIN       0x08u
#define STARK_LAB_I2C_ADDR_MAX       0x77u
#define STARK_LAB_I2C_PN532          0x24u
#define STARK_LAB_I2C_W_MAX          10u /* a 16-bit register and 8 data bytes */
#define STARK_LAB_I2C_R_MAX          16u
#define STARK_LAB_I2C_HZ_STD         100000u
#define STARK_LAB_I2C_HZ_FAST        400000u
#define STARK_LAB_I2C_TIMEOUT_MS_MAX 1000u

/*
 * An address-only check: STARK_OK on an ACK, STARK_ERR_NOT_FOUND on a NACK, STARK_ERR_TIMEOUT on
 * a timeout, STARK_ERR_IO on any other bus error, STARK_ERR_INVALID_ARG for an address outside the
 * range or a timeout of 0 or more than STARK_LAB_I2C_TIMEOUT_MS_MAX. STARK_ERR_NOT_SUPPORTED when
 * the board has no I2C bus. Needs no open (the bus belongs to stark_board).
 */
stark_err_t stark_lab_transport_i2c_probe(uint8_t addr, uint32_t timeout_ms);

/*
 * One transaction at `hz` (STARK_LAB_I2C_HZ_STD or _FAST): `wn` bytes written (0 to
 * STARK_LAB_I2C_W_MAX) and then `rn` bytes read (0 to STARK_LAB_I2C_R_MAX) after a repeated start;
 * at least one of them is not zero (else STARK_ERR_INVALID_ARG, as for a speed that is not
 * 100 000 or 400 000 Hz). A module ID EEPROM address takes only a one-byte pointer and a
 * read: a write (`rn` == 0) or more than one byte before the read is refused with
 * STARK_ERR_NOT_SUPPORTED; any transaction with the PN532 is refused while NFC is confirmed. Errors
 * as for the probe; an address NACK is STARK_ERR_NOT_FOUND.
 */
stark_err_t stark_lab_transport_i2c_xfer(uint8_t addr, uint32_t hz, const uint8_t *w, size_t wn,
                                         uint8_t *r, size_t rn, uint32_t timeout_ms);

static inline bool stark_lab_i2c_hz_ok(uint32_t hz)
{
    return hz == STARK_LAB_I2C_HZ_STD || hz == STARK_LAB_I2C_HZ_FAST;
}

/* True when `addr` is one of the module slots' ID EEPROM addresses in the board's pin map. */
static inline bool stark_lab_i2c_is_module_eeprom(const stark_board_pins_t *pins, uint8_t addr)
{
    for (size_t i = 0; i < STARK_BOARD_MODULE_SLOTS; i++) {
        if (pins->module[i].eeprom_addr != 0u && pins->module[i].eeprom_addr == addr) {
            return true;
        }
    }
    return false;
}

/* True when the arguments of a probe or transaction are well formed: the address, the sizes (at
 * least one byte moves), the speed (xfer only: pass 0 for a probe) and the timeout. */
static inline bool stark_lab_i2c_args_ok(uint8_t addr, size_t wn, size_t rn, uint32_t hz,
                                         uint32_t timeout_ms)
{
    return addr >= STARK_LAB_I2C_ADDR_MIN && addr <= STARK_LAB_I2C_ADDR_MAX &&
           wn <= STARK_LAB_I2C_W_MAX && rn <= STARK_LAB_I2C_R_MAX &&
           ((wn == 0u && rn == 0u) == (hz == 0u)) && (hz == 0u || stark_lab_i2c_hz_ok(hz)) &&
           timeout_ms > 0u && timeout_ms <= STARK_LAB_I2C_TIMEOUT_MS_MAX;
}

/*
 * The shared I2C guard as a pure rule: `nfc_confirmed` is stark_module_caps() & STARK_CAP_NFC.
 * A write to a module ID EEPROM (rn == 0, or more than a one-byte pointer before a read) and any
 * transaction with the PN532 while NFC is confirmed are refused.
 */
static inline bool stark_lab_i2c_allowed(const stark_board_pins_t *pins, bool nfc_confirmed,
                                         uint8_t addr, size_t wn, size_t rn)
{
    /* NOTE: a read that carries more than a one-byte pointer would write the rest into the chip,
     * so a module ID EEPROM takes exactly: no write at all, or a one-byte pointer and a read. */
    if (stark_lab_i2c_is_module_eeprom(pins, addr) && (rn == 0u || wn > 1u)) {
        return false;
    }
    return !(addr == STARK_LAB_I2C_PN532 && nfc_confirmed);
}

/* ---- the lab UART (STARK-0705, ADR-0023 §7) ----------------------------------------------------
 *
 * UART1 on the board's two lab pins (stark_board_pins()->lab_uart_tx / lab_uart_rx: 38 and 39),
 * 3.3 V TTL, installed without a driver task. The pins are the lab service's to hand out (the UART
 * tool holds them through stark_lab_acquire(), so the GPIO app cannot drive them meanwhile); the
 * transport re-checks them against stark_board anyway (stark_lab_uart_pins_ok(), defence in depth)
 * and touches no other pin. Callers are serialised by the service.
 *
 * `fmt` is an index into the format list 8N1, 8E1, 8O1, 8N2, 7E1 (lab_uart.h); `baud` is any value
 * from STARK_LAB_UART_BAUD_MIN to STARK_LAB_UART_BAUD_MAX (the service offers the standard list).
 * The driver is installed with a small event queue so that framing errors, parity errors and
 * overruns can be counted (ESP-IDF's UART driver reports them nowhere else); every read drains
 * it, and an error storm faster than the reads therefore counts a lower bound.
 */
#define STARK_LAB_UART_BAUD_MIN       1200u
#define STARK_LAB_UART_BAUD_MAX       921600u
#define STARK_LAB_UART_FORMATS        5u
#define STARK_LAB_UART_RXBUF_MIN      256u
#define STARK_LAB_UART_RXBUF_MAX      8192u
#define STARK_LAB_UART_READ_MAX       256u /* one read returns at most this many bytes */
#define STARK_LAB_UART_WRITE_MAX      64u  /* one write takes at most this many bytes */
#define STARK_LAB_UART_TIMEOUT_MS_MAX 5000u

/* Errors seen by the receiver since the previous read. */
typedef struct {
    uint32_t framing, parity, overrun; /* overrun: the driver's FIFO or buffer overflowed */
} stark_lab_uart_err_t;

static inline bool stark_lab_uart_args_ok(uint32_t baud, uint8_t fmt, size_t rx_buf)
{
    return baud >= STARK_LAB_UART_BAUD_MIN && baud <= STARK_LAB_UART_BAUD_MAX &&
           fmt < STARK_LAB_UART_FORMATS && rx_buf >= STARK_LAB_UART_RXBUF_MIN &&
           rx_buf <= STARK_LAB_UART_RXBUF_MAX;
}

/*
 * The guard of the UART group, shared by the hardware transport, the test image's and the host
 * shim: the board names both UART pins and stark_board classifies both as LAB pins (always offered;
 * a SLOT pin or a reserved one is never the UART's).
 */
static inline bool stark_lab_uart_pins_ok(const stark_board_pins_t *pins)
{
    return pins->lab_uart_tx >= 0 && pins->lab_uart_rx >= 0 &&
           stark_board_pin_class(pins->lab_uart_tx, NULL) == STARK_PIN_LAB &&
           stark_board_pin_class(pins->lab_uart_rx, NULL) == STARK_PIN_LAB;
}

/*
 * Installs UART1 at `baud` and format `fmt` with a receive buffer of `rx_buf` bytes and routes RX
 * to the lab RX pin. `tx` also routes TX to the lab TX pin: without it the TX pad is never touched,
 * so a monitor drives nothing (the service passes true only after the TX pin was armed).
 * STARK_ERR_INVALID_ARG outside the ranges, STARK_ERR_STATE when the driver is already installed
 * or the transport is closed, STARK_ERR_NO_MEM, STARK_ERR_IO. A failure leaves nothing installed.
 */
stark_err_t stark_lab_transport_uart_open(uint32_t baud, uint8_t fmt, size_t rx_buf, bool tx);

/*
 * Waits up to `timeout_ms` (0 to STARK_LAB_UART_TIMEOUT_MS_MAX) for up to `max` (1 to
 * STARK_LAB_UART_READ_MAX) bytes and returns how many came: 0 on a timeout, a negative stark_err_t
 * value when the driver is not installed or failed. `errs` (may be NULL) receives the errors seen
 * since the previous read.
 */
int stark_lab_transport_uart_read(uint8_t *buf, size_t max, uint32_t timeout_ms,
                                  stark_lab_uart_err_t *errs);

/*
 * Sends `n` (1 to STARK_LAB_UART_WRITE_MAX) bytes and waits up to `timeout_ms` for the last one to
 * leave the shifter. Returns the bytes sent, or a negative stark_err_t value: STARK_ERR_STATE when
 * the driver is not installed or was opened without TX, STARK_ERR_TIMEOUT when the bytes did not
 * leave in time, STARK_ERR_INVALID_ARG for a bad length.
 */
int stark_lab_transport_uart_write(const uint8_t *buf, size_t n, uint32_t timeout_ms);

/* Changes the baud of the installed driver (the format stays). STARK_ERR_STATE when not installed.
 */
stark_err_t stark_lab_transport_uart_set_baud(uint32_t baud);

/* Removes the driver and returns both pads to hi-Z (input, no pull). Idempotent. */
void stark_lab_transport_uart_close(void);

/* ---- SPI on a leased module slot (STARK-0706, ADR-0023 §6) -------------------------------------
 *
 * One transaction on a slot's own chip select, through stark_module's lease and SPI device (the
 * shared SPI2 bus belongs to the panel, the card and the modules; ESP-IDF's bus lock keeps this
 * transaction from interleaving with theirs). The only chip select it can ever assert is the
 * lease's: there is no call that names a pin, so the panel's and the card's devices cannot be
 * addressed. The call is refused, before the bus is touched, unless the slot's module declares
 * MOD_SPI and no function bit (stark_lab_spi_entry_ok()): a radio, the NFC reader or anything
 * unknown owns its slot, because a raw write to a CC1101 could set PATABLE or strobe STX and walk
 * around the transmit policy (ADR-0022 §6). The same guard is applied by the test image's
 * transport and the host shim, so none can do what another refuses (defence in depth,
 * SECURITY_SCOPE §4.6; the service applies it first).
 */
#define STARK_LAB_SPI_BYTES_MAX   32u
#define STARK_LAB_SPI_CLOCK_COUNT 6u

static inline uint32_t stark_lab_spi_clock_hz(size_t index)
{
    static const uint32_t k_hz[STARK_LAB_SPI_CLOCK_COUNT] = {100000u,  250000u,  500000u,
                                                             1000000u, 2000000u, 4000000u};
    return index < STARK_LAB_SPI_CLOCK_COUNT ? k_hz[index] : 0u;
}

static inline bool stark_lab_spi_hz_ok(uint32_t hz)
{
    for (size_t i = 0; i < STARK_LAB_SPI_CLOCK_COUNT; i++) {
        if (stark_lab_spi_clock_hz(i) == hz) {
            return true;
        }
    }
    return false;
}

/* True when the arguments of a transaction are well formed: 1 to 32 bytes, a listed clock, mode 0
 * to 3. */
static inline bool stark_lab_spi_args_ok(uint32_t hz, uint8_t mode, size_t n)
{
    return n >= 1u && n <= STARK_LAB_SPI_BYTES_MAX && mode <= 3u && stark_lab_spi_hz_ok(hz);
}

/*
 * The eligibility rule as a pure function of a table entry: a slot that reads KNOWN and whose
 * descriptor declares MOD_SPI and nothing else except MOD_IRQ. NFC, Sub-GHz, the IR bits and any
 * bit this firmware does not know are functions: their slot is a driver's.
 */
static inline bool stark_lab_spi_entry_ok(const stark_module_entry_t *e)
{
    return e->kind == STARK_MODULE_SLOT && e->state == STARK_MODULE_KNOWN &&
           (e->declared & STARK_CAP_MOD_SPI) != 0u &&
           (e->declared & ~(uint32_t)(STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ)) == 0u;
}

/* True when `list` has slot `slot` and it passes stark_lab_spi_entry_ok(). */
static inline bool stark_lab_spi_slot_ok(const stark_module_entry_t *list, size_t n, int slot)
{
    for (size_t i = 0; i < n; i++) {
        if (list[i].kind == STARK_MODULE_SLOT && (int)list[i].slot == slot) {
            return stark_lab_spi_entry_ok(&list[i]);
        }
    }
    return false;
}

/*
 * One transaction: `n` bytes (1 to STARK_LAB_SPI_BYTES_MAX) are clocked out of `tx` while `n` are
 * clocked into `rx`, at `hz` (one of the six clocks, 100 kHz to 4 MHz) and SPI mode `mode` (0 to
 * 3), on the lease's chip select; the device is attached for the call and detached again, so
 * nothing outlives it. STARK_ERR_INVALID_ARG for a NULL, a length, a clock or a mode out of
 * range, STARK_ERR_NOT_SUPPORTED when the slot is not eligible (above), STARK_ERR_STATE for a lease
 * that is stale (the module left), STARK_ERR_BUSY while another driver has a device on the slot,
 * STARK_ERR_IO for a bus failure. The caller holds the lease; the service claims and releases it.
 */
stark_err_t stark_lab_transport_spi_xfer(const stark_module_lease_t *l, uint32_t hz, uint8_t mode,
                                         const uint8_t *tx, uint8_t *rx, size_t n);
