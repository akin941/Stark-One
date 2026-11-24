/*
 * stark_lab.h — L3 lab service (TASKS.md STARK-0701…0707, ADR-0023): the one place that
 * decides which pins a lab tool may touch and checks every pin call against it. Today the
 * pad group (STARK-0701), the PWM and pulse outputs (STARK-0702) and the frequency and duty
 * measurement (STARK-0703), whose user is the GPIO app; the shared I2C bus (STARK-0704: scan,
 * hints, register read, dump and write, whose user is the I2C app) and the lab UART (STARK-0705:
 * monitor, send, loopback test and auto-baud on the two lab pins, whose user is the UART app) and
 * the SPI probe (STARK-0706: one confirmed transaction on a leased module slot that declares no
 * function, whose user is the SPI app); the self-test joins in STARK-0707.
 *
 * Pins are named only through stark_board (stark_board_pin_class(), stark_board_lab_pins()):
 * a LAB pin is always offered, a SLOT pin only while its slot reads ABSENT, everything else
 * never (HARDWARE.md §2.4). Every call checks the offer first, then the hold, and a pin that
 * is not offered returns STARK_ERR_NOT_SUPPORTED before any hardware is touched. Driving
 * needs an arm after the app's confirmation; every pin is released to hi-Z (input, no pull)
 * on release, withdrawal and failure. Changes of the offer are published as
 * STARK_EVT_SYSTEM / STARK_SYS_LAB with system.arg = STARK_LAB_EVT_*.
 *
 * Frequency and duty (STARK-0703) are measured from timestamped edges (stark_lab_measure(),
 * ADR-0023 §4); the arithmetic is the pure lab_meas.c.
 *
 * Any task may call; one mutex serialises, and nothing is allocated after init (the UART session
 * allocates its 4 kB ring when it opens and frees it when it closes).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stark_board.h"
#include "stark_err.h"

#define STARK_SYS_LAB              8u

#define STARK_LAB_EVT_PINS         1 /* the offer changed, or a held pin was withdrawn */
#define STARK_LAB_EVT_MEASURE_DONE 2 /* a stark_lab_measure() has a result (STARK-0703) */
/* 3 stays reserved: the I2C sweep is a synchronous call (STARK-0704), so it has no SCAN_DONE */
#define STARK_LAB_EVT_UART_DATA    4 /* the UART monitor received bytes (at most 10 a second) */
#define STARK_LAB_EVT_UART_ERROR   5 /* ... or counted a framing, parity or overrun error */
#define STARK_LAB_EVT_SPI_DONE     6 /* an SPI probe transaction finished (STARK-0706) */

typedef enum { STARK_LAB_PULL_NONE, STARK_LAB_PULL_UP, STARK_LAB_PULL_DOWN } stark_lab_pull_t;

/* What a pad is doing, found with each pull applied for 2 ms. */
typedef enum {
    STARK_LAB_PIN_FLOATING, /* follows the pull: nothing drives it */
    STARK_LAB_PIN_HIGH,     /* driven high against the pull-down */
    STARK_LAB_PIN_LOW       /* driven low against the pull-up */
} stark_lab_pinstate_t;

/* What a held pin is doing now, as the service last set it. */
typedef enum {
    STARK_LAB_MODE_HIZ, /* input, no pull */
    STARK_LAB_MODE_IN,  /* input with a pull (stark_lab_info_t.pull) */
    STARK_LAB_MODE_OUT, /* driven to stark_lab_info_t.level */
    STARK_LAB_MODE_PWM  /* the PWM output: hz and duty */
} stark_lab_mode_t;

typedef struct {
    stark_lab_mode_t mode;
    stark_lab_pull_t pull; /* IN */
    bool level;            /* OUT: the level it was driven to and read back at */
    uint32_t hz;           /* PWM */
    uint32_t duty_pct;     /* PWM */
    bool armed;
} stark_lab_info_t;

typedef struct {
    int pin;
    const char *name; /* "LAB_UART_TX", "MOD0_CS", ... */
    stark_pin_class_t cls;
    bool offered; /* as of now: LAB, or SLOT with its slot reading ABSENT */
    bool held;
} stark_lab_pin_t;

/*
 * Opens the transport and logs `lab: pins <offered pins>` (`lab: pins` then one number per pin)
 * from the module table as it is: call after stark_module_init() has read it. Creates no task
 * and subscribes to no event (the composition root calls stark_lab_refresh()). Never
 * boot-critical. STARK_ERR_STATE when already initialised.
 */
stark_err_t stark_lab_init(void);

/* The whole offer table, with the offered and held flags as of now; returns how many exist. */
size_t stark_lab_pins(stark_lab_pin_t *out, size_t max);

/*
 * Takes a pin for `owner` (a short name, copied) and releases it to hi-Z first.
 * STARK_ERR_NOT_SUPPORTED when it is not offered, STARK_ERR_BUSY when held,
 * STARK_ERR_INVALID_ARG without an owner.
 */
stark_err_t stark_lab_acquire(int pin, const char *owner);

/* Hi-Z, no pull, arm cleared. Any task; a no-op for a pin that is not held. */
void stark_lab_release(int pin);
void stark_lab_release_all(void);

/*
 * The arm: the app calls it after the user pressed OK on a dialog naming the pin and
 * "connected hardware may be damaged". Per pin, RAM only; cleared by release, withdrawal, a
 * failed drive and leaving the app. STARK_ERR_STATE when the pin is not held.
 */
stark_err_t stark_lab_arm(int pin);

/* Input with an internal pull (about 45 kOhm). STARK_ERR_STATE when not held. */
stark_err_t stark_lab_input(int pin, stark_lab_pull_t pull);

/* Input, no pull, the pad released but the pin kept: held, armed and mode HIZ. STARK_ERR_STATE
 * when not held. */
stark_err_t stark_lab_hiz(int pin);

/* The level on the pad. STARK_ERR_STATE when not held. */
stark_err_t stark_lab_read(int pin, bool *level);

/*
 * What the pad is doing: each pull for 2 ms, then no pull. The pin is left an input. A pad
 * that reads high with the pull-down is driven high, one that reads low with the pull-up is
 * driven low, one that follows the pulls is floating; any other result is STARK_ERR_IO (a
 * pad that moved while it was looked at).
 */
stark_err_t stark_lab_state(int pin, stark_lab_pinstate_t *st);

/*
 * Drives the pad at the weakest strength. STARK_ERR_STATE unless held and armed. The pad is
 * read back after 1 ms; a mismatch (contention) puts the pin in hi-Z, clears the arm, logs
 * `lab: contention pin <n>` and returns STARK_ERR_IO. A transport failure does the same.
 */
stark_err_t stark_lab_write(int pin, bool level);

/*
 * What the pin is doing now (the last thing the service did to it). STARK_ERR_NOT_SUPPORTED when
 * it is not offered, STARK_ERR_STATE when not held. Drives nothing.
 */
stark_err_t stark_lab_info(int pin, stark_lab_info_t *out);

/*
 * Flips an output driven through stark_lab_write(): STARK_ERR_STATE unless the pin is held,
 * armed and currently an output. Otherwise as stark_lab_write() (read-back, contention).
 */
stark_err_t stark_lab_toggle(int pin);

/*
 * PWM on the pin (STARK-0702): `hz` 100 to 40 000, `duty_pct` 0 to 100, else
 * STARK_ERR_INVALID_ARG; STARK_ERR_STATE unless held and armed; STARK_ERR_BUSY while another pin
 * has the PWM (the same pin may be started again with new values). Weakest drive strength, LEDC
 * channel 2 only. The output is not read back (a square wave has no one level): the arm's
 * dialog is the guard. A failure puts the pin in hi-Z and clears the arm.
 */
stark_err_t stark_lab_pwm_start(int pin, uint32_t hz, uint32_t duty_pct);

/* Stops the PWM and releases its pad to hi-Z; the pin stays held and armed. A no-op without one. */
void stark_lab_pwm_stop(void);

/*
 * One pulse (STARK-0702): `level` for `us` (10, 100, 1000, 10 000, 100 000 or 1 000 000, else
 * STARK_ERR_INVALID_ARG), STARK_ERR_STATE unless held and armed. The pad is first driven to the
 * opposite level and read back (contention is detected there and ends in hi-Z, as for a write), the
 * pulse follows, and the pad returns to the mode it had. Blocks the caller for the width: call it
 * from the app worker. A pulse is one pulse; nothing repeats.
 */
stark_err_t stark_lab_pulse(int pin, bool level, uint32_t us);

/* Ends a pulse that is waiting, early (within 10 ms). Any task, takes no lock. */
void stark_lab_pulse_cancel(void);

/* ---- the GPIO app's steps (lab_pwm.c, pure; STARK-0702) ------------------------------------ */

/*
 * One step of the frequency row: the leading digit moves by one unit of its decade (100, 200, ...
 * 900, 1000, 2000, ... 9000, 10000, 20000, ... 40000), so every decade is crossed. A value off
 * that ladder moves to the next rung in the direction. Clamped to the limits: at an edge it
 * returns `hz` unchanged. dir is the sign of the step (zero leaves it unchanged).
 */
uint32_t stark_lab_pwm_step_hz(uint32_t hz, int dir);

/* One step of the duty row: 1 %, clamped to 0..100; unchanged at an edge. */
uint32_t stark_lab_pwm_step_duty(uint32_t duty_pct, int dir);

/* The next width in the direction, clamped to the list; an unlisted value gives 100 us. */
uint32_t stark_lab_pulse_step(uint32_t us, int dir);

/* ---- frequency and duty from edges (lab_meas.c, pure; STARK-0703, ADR-0023 §4) ------------ */

#define STARK_LAB_MEAS_RING   1024u /* timestamps kept per window: the ring the caller supplies */
#define STARK_LAB_MEAS_HZ_MIN 1u    /* the measured range, hertz */
#define STARK_LAB_MEAS_HZ_MAX 50000u

typedef enum {
    STARK_LAB_MEAS_OK,        /* hz_x100, duty_x10 and period_ticks are valid */
    STARK_LAB_MEAS_TOO_FAST,  /* above 50 kHz: not measured */
    STARK_LAB_MEAS_TOO_SLOW,  /* below 1 Hz, or fewer than two rising edges in the window */
    STARK_LAB_MEAS_NO_SIGNAL, /* no edge at all: a steady line, high or low (`steady_high`) */
    STARK_LAB_MEAS_OVERFLOW,  /* more edges than the ring holds: reported, never guessed */
    STARK_LAB_MEAS_GLITCH,    /* a pulse shorter than 1 us (or two edges at one instant) */
    STARK_LAB_MEAS_INVALID    /* timestamps that go backwards, a zero clock, a ring too long */
} stark_lab_meas_status_t;

typedef struct {
    stark_lab_meas_status_t status;
    uint32_t hz_x100;      /* frequency in 0.01 Hz units; 0 unless OK */
    uint16_t duty_x10;     /* duty in 0.1 % units; 0 unless OK */
    uint32_t period_ticks; /* the mean period in clock ticks; 0 unless OK */
    bool steady_high;      /* NO_SIGNAL: the level the line sits at */
    uint32_t edges;        /* edges the window kept */
} stark_lab_meas_t;

/*
 * Measures the signal on a held input pin (mode IN): a 20 ms pre-gate counts edges to estimate
 * the rate (more than 50 kHz is TOO_FAST at once), then a window of 10 ms to 4 s sized so the
 * expected edges fit the ring is captured and lab_meas_compute() turns it into the result. The
 * caller supplies the ring (`ring`, `cap` >= 2 timestamps, at most STARK_LAB_MEAS_RING are
 * used; the service allocates nothing). The pad is an input throughout and returns to its
 * pull; the edge interrupt exists for the two windows only. Takes no arm: nothing is driven.
 * STARK_ERR_STATE unless the pin is held and an input, or when the measurement was cancelled
 * (nothing is returned); STARK_ERR_NOT_SUPPORTED for a pin that is not offered;
 * STARK_ERR_INVALID_ARG for a NULL argument or a short ring; a transport failure puts the pin in
 * hi-Z, clears its arm and returns STARK_ERR_IO. Blocks the caller for up to 4.1 s: call it from
 * the app worker. Publishes STARK_LAB_EVT_MEASURE_DONE when a result is ready. Logs `lab: measure
 * <text>`.
 */
stark_err_t stark_lab_measure(int pin, uint32_t *ring, size_t cap, stark_lab_meas_t *out);

/* Ends a measurement that is waiting, early (within 10 ms). Any task, takes no lock. */
void stark_lab_measure_cancel(void);

/* "1.000 kHz duty 25.0 %", "250.00 Hz duty 50.0 %", "> 50 kHz, not measured", "< 1 Hz",
 * "no signal (high)", "overflow: too many edges", "glitches (< 1 us)", "invalid timestamps". */
void stark_lab_meas_text(const stark_lab_meas_t *m, char *buf, size_t n);

/* The log form: "1000.0 Hz duty 25.0 %", "too fast", "too slow", "no signal (high)", "overflow",
 * "glitch", "invalid". */
void stark_lab_meas_log_text(const stark_lab_meas_t *m, char *buf, size_t n);

/* ---- the shared I2C bus (lab_i2c.c, lab_i2c_hints.c; STARK-0704, ADR-0023 §5) -----------------
 */

#define STARK_LAB_I2C_SPEED_100K 100000u /* the two speeds, in hertz */
#define STARK_LAB_I2C_SPEED_400K 400000u
#define STARK_LAB_I2C_ADDRS      112u /* 0x08 to 0x77 */
#define STARK_LAB_I2C_DUMP_SIZE  256u

typedef struct {
    uint8_t addr[STARK_LAB_I2C_ADDRS]; /* the addresses that answered, ascending */
    size_t n;
    stark_err_t status; /* STARK_OK, or STARK_ERR_IO when errors ended the sweep early */
    uint8_t errors;     /* probes that timed out or failed (a NACK is not one) */
} stark_lab_scan_t;

/*
 * The whole sweep: an address-only probe of 0x08 to 0x77, 5 ms each, in order. Three errors in a
 * row end it with STARK_ERR_IO and the list so far; a NACK is an empty address. Any task; at
 * most 112 probes whatever the bus does. Logs `lab: i2c scan found <0x23 0x3c ...|none>`, or
 * `lab: i2c scan bus error found <...>` when it was cut short.
 */
stark_err_t stark_lab_i2c_scan(stark_lab_scan_t *out);

/*
 * What an address is often used for, "may be ..." (30 characters at most), or NULL. A lookup by
 * address in a table of at most 48 generic entries: nothing is probed, nothing else is known.
 */
const char *stark_lab_i2c_hint(uint8_t addr);

/*
 * Register read of 1 to 16 bytes at 100000 or 400000 Hz: the register goes out as one byte, or
 * two when `reg16`, and the bytes follow a repeated start. No arm (a read is not a write, but it
 * can change a device's state: the app says so). STARK_ERR_INVALID_ARG for a bad address, length,
 * register or speed; STARK_ERR_NOT_SUPPORTED for a refusal (a 16-bit register on a module ID
 * EEPROM, the PN532 while NFC is confirmed); STARK_ERR_NOT_FOUND for a NACK, STARK_ERR_TIMEOUT or
 * STARK_ERR_IO from the bus. Logs `lab: i2c read 0x50 reg 0x00 00 00 ...` and, refused, `lab: i2c
 * read refused: <reason>`.
 */
stark_err_t stark_lab_i2c_read(uint8_t addr, uint16_t reg, bool reg16, uint8_t *buf, size_t n,
                               uint32_t hz);

/*
 * 256 bytes from register 0 (16 reads of 16), after a confirmation: STARK_ERR_STATE unless
 * stark_lab_i2c_arm(stark_lab_i2c_dump_fingerprint(addr)) came first; the arm is consumed by the
 * call. Otherwise as stark_lab_i2c_read(). Logs `lab: i2c dump 0x68 ok` or `... <error>`.
 */
stark_err_t stark_lab_i2c_dump(uint8_t addr, uint8_t *buf256, uint32_t hz);

/*
 * Register write of 1 to 8 bytes in one transaction, after a confirmation: STARK_ERR_STATE unless
 * stark_lab_i2c_arm(stark_lab_i2c_write_fingerprint(...)) of exactly these arguments came first
 * (the arm is consumed by the call, so one confirmation buys one write, and a different payload
 * is refused). A module ID EEPROM (the board's `module[i].eeprom_addr`, ADR-0021 §2: a descriptor
 * is never written) and the PN532 while NFC is confirmed are refused with STARK_ERR_NOT_SUPPORTED
 * before the bus is touched, and no arm is needed to be refused. Logs `lab: i2c write 0x68 reg
 * 0x6b 01 ok`, `... <error>` and `lab: i2c write refused: module eeprom` / `nfc reader`.
 */
stark_err_t stark_lab_i2c_write(uint8_t addr, uint16_t reg, bool reg16, const uint8_t *v, size_t n,
                                uint32_t hz);

/* The app's OK on the dialog it showed: one confirmation, tied to a fingerprint of its content. A
 * second call replaces the first; an arm older than 30 s is stale. */
stark_err_t stark_lab_i2c_arm(uint32_t fingerprint);

/* Clears an arm that no call has consumed (leaving a view, the app stopping). Any task. */
void stark_lab_i2c_disarm(void);

/*
 * Why a transaction of this kind would be refused now, or NULL: "module eeprom" for a write to a
 * module ID EEPROM (`write`) or a 16-bit register read of one, "nfc reader" for the PN532 while
 * NFC is confirmed. Lets the app say so before it asks for a confirmation; the calls above refuse
 * all the same.
 */
const char *stark_lab_i2c_refusal(uint8_t addr, bool reg16, bool write);

/* The fingerprint of a confirmation's content (lab_i2c.h): address, register and width, count
 * and bytes; and address alone for a dump. Pure. */
uint32_t stark_lab_i2c_write_fingerprint(uint8_t addr, uint16_t reg, bool reg16, const uint8_t *v,
                                         size_t n);
uint32_t stark_lab_i2c_dump_fingerprint(uint8_t addr);

/* "01 ab ff" (lower case, spaces) and "01abff": whole bytes that fit, NUL terminated, returns the
 * length. Pure. */
size_t stark_lab_i2c_hex(const uint8_t *b, size_t n, char *buf, size_t sz);
size_t stark_lab_i2c_hex_packed(const uint8_t *b, size_t n, char *buf, size_t sz);

/* ---- the lab UART (lab_uart.c, lab_autobaud.c, lab_uart_svc.c; STARK-0705, ADR-0023 §7) -------
 *
 * UART1 on the two lab pins (stark_board's `lab_uart_tx` / `lab_uart_rx`), 3.3 V TTL only. The
 * tool holds both pins through the same hold the GPIO app uses (owner "uart"), so the two exclude
 * each other: while one holds a pin the other's stark_lab_acquire() is STARK_ERR_BUSY. A session
 * opens receive-only (the TX pad is never touched, so a monitor drives nothing); sending needs the
 * TX pin armed after the app's confirmation, and arming switches TX on. The ring, the driver's
 * buffer and the session exist from stark_lab_uart_open() to stark_lab_uart_close() only, and
 * every exit path closes: the driver is removed and both pads return to hi-Z.
 *
 * Calls may come from any task, but one operation at a time: the app's worker polls, the UI task
 * reads the ring and the counters (a short lock) and closes. Received bytes never reach the console
 * log: it carries counts only.
 */
#define STARK_LAB_UART_BAUDS    11u             /* 1 200 to 921 600 */
#define STARK_LAB_UART_FMTS     5u              /* 8N1 8E1 8O1 8N2 7E1 */
#define STARK_LAB_UART_RING     4096u           /* bytes of received data kept for the monitor */
#define STARK_LAB_UART_SEND_MAX 32u             /* bytes of one send */
#define STARK_LAB_UART_FILE_MAX 1024u           /* bytes of the send file */
#define STARK_LAB_UART_LOOP_LEN 64u             /* the loopback pattern: 00 01 ... 3f */
#define STARK_LAB_UART_LOG_MAX  (1024u * 1024u) /* bytes of one log file */
#define STARK_LAB_UART_LOG_BUF  2048u           /* the log's write buffer */
#define STARK_LAB_UART_LOG_SYNC 4096u           /* bytes between two flushes to the medium */
#define STARK_LAB_UART_CHUNK    64u             /* the most one poll takes */

/* The baud list by index (0 past the end) and the format names (NULL past the end). */
uint32_t stark_lab_uart_baud(size_t index);
const char *stark_lab_uart_fmt_name(size_t index);

typedef enum {
    STARK_LAB_LOOP_OK,          /* every byte came back unchanged and the line reported nothing */
    STARK_LAB_LOOP_MISMATCH,    /* bytes differ, or fewer came back than were sent */
    STARK_LAB_LOOP_LINE_ERRORS, /* the bytes match but the receiver counted errors */
    STARK_LAB_LOOP_NO_DATA      /* nothing came back */
} stark_lab_loop_status_t;

typedef struct {
    stark_lab_loop_status_t status;
    uint32_t sent;
    uint32_t received;
    uint32_t errors;        /* differing bytes, missing bytes, and the receiver's own counts */
    int32_t first_mismatch; /* offset of the first differing or missing byte, -1 for none */
    uint32_t framing, parity, overrun; /* what the receiver counted during the test */
} stark_lab_loop_t;

typedef enum {
    STARK_LAB_BAUD_OK,       /* `baud` is a listed baud */
    STARK_LAB_BAUD_TOO_FEW,  /* fewer than 16 edges: a steady line or no stream */
    STARK_LAB_BAUD_NO_PULSE, /* every pulse was a glitch */
    STARK_LAB_BAUD_UNKNOWN,  /* a stream, but its bit time is near no listed baud */
    STARK_LAB_BAUD_INVALID   /* stamps that go backwards, no clock, too many edges */
} stark_lab_autobaud_status_t;

typedef struct {
    stark_lab_autobaud_status_t status;
    uint32_t baud;      /* the listed baud, 0 unless OK */
    uint32_t raw_baud;  /* what the refined bit time gives, 0 when none was found */
    uint32_t bit_ticks; /* the refined bit time in clock ticks, 0 when none was found */
    uint32_t edges;     /* edges captured */
} stark_lab_autobaud_t;

typedef struct {
    bool open;
    bool tx; /* TX is routed to its pad (armed) */
    size_t baud_index, fmt_index;
    uint32_t rx;   /* bytes received since the session opened or was cleared */
    uint32_t lost; /* of those, bytes the ring overwrote before they were looked at */
    uint32_t framing, parity, overrun; /* what the receiver counted since then */
    size_t buffered;                   /* bytes in the ring */
} stark_lab_uart_stats_t;

/*
 * Opens a receive-only session at the listed baud and format (indices): holds both lab pins,
 * installs the driver (a 2 kB buffer) and allocates the 4 kB ring. STARK_ERR_INVALID_ARG for an
 * index past its list, STARK_ERR_STATE when open already, STARK_ERR_BUSY when the GPIO app (or
 * anything) holds either pin, STARK_ERR_NO_MEM. A failure holds nothing. Logs `lab: uart open
 * 115200 8N1`.
 */
stark_err_t stark_lab_uart_open(size_t baud_index, size_t fmt_index);

/*
 * Arms the TX pin and routes TX to its pad: the app calls it only after the user pressed OK on a
 * dialog naming the pin and "connected hardware may be damaged" (the same arm as stark_lab_arm(),
 * ADR-0023 §3). The driver is reinstalled with TX; the ring and the counters stay. Idempotent.
 * STARK_ERR_STATE unless open. Logs `lab: uart tx on`.
 */
stark_err_t stark_lab_uart_tx_enable(void);

/*
 * A new baud and/or format: the driver is reinstalled (or only its baud changed), the ring and the
 * counters are cleared. STARK_ERR_STATE unless open, STARK_ERR_INVALID_ARG for a bad index.
 */
stark_err_t stark_lab_uart_set_format(size_t baud_index, size_t fmt_index);

/*
 * Waits up to `timeout_ms` for bytes (at most min(max, STARK_LAB_UART_CHUNK)), appends them to the
 * ring, counts errors, and copies them to `chunk` for the caller's own use (the log) with `*got`
 * their number: 0 on a timeout. STARK_ERR_STATE unless open; a transport failure closes the session
 * (pins released) and returns STARK_ERR_IO. Publishes STARK_LAB_EVT_UART_DATA and _ERROR, each at
 * most ten times a second. Runs on the app worker.
 */
stark_err_t stark_lab_uart_poll(uint8_t *chunk, size_t max, uint32_t timeout_ms, size_t *got);

/* Copies up to `max` bytes of the ring starting `skip` bytes after the oldest; returns how many. A
 * short lock: any task. */
size_t stark_lab_uart_ring_read(size_t skip, uint8_t *out, size_t max);

/* The counters and the session's settings; all zero and closed before open. Any task. */
void stark_lab_uart_stats(stark_lab_uart_stats_t *out);

/* Empties the ring and zeroes the counters, and drops what the driver holds. */
void stark_lab_uart_clear(void);

/*
 * Sends 1 to STARK_LAB_UART_SEND_MAX bytes, once. STARK_ERR_INVALID_ARG for another length,
 * STARK_ERR_STATE unless the session is open with TX on. A transport failure closes the session and
 * returns STARK_ERR_IO. Logs `lab: uart send <n> bytes`. Runs on the app worker or the UI task: it
 * waits for the bytes to leave (up to 32 characters at 1 200 baud: 0.3 s).
 */
stark_err_t stark_lab_uart_send(const uint8_t *buf, size_t n);

/*
 * The loopback bridge: sends the 64-byte pattern 00..3f as two writes of 32, listens for up to
 * twice the wire time plus 20 ms for the same bytes back (TX wired to RX, or an echoing peer) and
 * judges them. The receive buffer is emptied first. STARK_ERR_STATE unless open with TX on, or when
 * cancelled; STARK_ERR_IO on a transport failure (the session is closed). Logs
 * `lab: uart loop 64/64 ok`, `... mismatch first <n>`, `... errors <n>`, `0/64 no data`. At most
 * about 1.6 s (1 200 baud): call it from the app worker.
 */
stark_err_t stark_lab_uart_loop_test(stark_lab_loop_t *out);

/*
 * Auto-baud: captures the edges of the RX pad for up to 2 s and names the standard baud they fit
 * within 3 %, else `unknown`. Needs the session closed: it holds the RX pin only, releases it
 * before it returns, and so leaves the pins as it found them. STARK_ERR_STATE when a session is
 * open or when cancelled, STARK_ERR_BUSY when the pin is held, STARK_ERR_NO_MEM. Logs `lab:
 * autobaud 38400` or `lab: autobaud unknown`. Runs on the app worker.
 */
stark_err_t stark_lab_uart_autobaud(stark_lab_autobaud_t *out);

/* Ends a loopback test or an auto-baud that is waiting, early (within 10 ms). Any task, no lock. */
void stark_lab_uart_cancel(void);

/* Removes the driver, frees the ring and releases both pins to hi-Z. Idempotent. Logs `lab: uart
 * closed`. */
void stark_lab_uart_close(void);

/* "64/64 ok", "64/64 differs at 0", "64/60 differs at 60", "64/64 line errors 2", "No data back"
 * for the app; "64/64 ok", "64/64 mismatch first 0", "64/64 errors 2", "0/64 no data" for the log.
 * Pure. */
void stark_lab_loop_text(const stark_lab_loop_t *r, char *buf, size_t n);
void stark_lab_loop_log_text(const stark_lab_loop_t *r, char *buf, size_t n);

/* "38400", "unknown", "too few edges", "no pulse", "invalid": the log and the app. Pure. */
const char *stark_lab_autobaud_text(const stark_lab_autobaud_t *r, char *buf, size_t n);

/* "00 11 ab" and printable ASCII with '.' for the rest: the monitor's rows. Whole characters that
 * fit; return the length. Pure. */
size_t stark_lab_uart_hex_row(const uint8_t *b, size_t n, char *buf, size_t sz);
size_t stark_lab_uart_ascii_row(const uint8_t *b, size_t n, char *buf, size_t sz);

typedef enum {
    STARK_LAB_FILE_OK,
    STARK_LAB_FILE_EMPTY,   /* nothing to send */
    STARK_LAB_FILE_TOO_BIG, /* more than 1 kB, or more than 32 bytes to send */
    STARK_LAB_FILE_BAD      /* not hex pairs after `hex:` and not printable ASCII */
} stark_lab_file_t;

/*
 * The send file, strictly: a file that starts with `hex:` holds pairs of hex digits separated by
 * white space; any other file is printable ASCII (0x20 to 0x7e) with one trailing LF or CRLF
 * allowed. At most STARK_LAB_UART_FILE_MAX bytes in and STARK_LAB_UART_SEND_MAX out; anything else
 * rejects the whole file. `*count` is the bytes produced. Pure.
 */
stark_lab_file_t stark_lab_uart_parse_send(const uint8_t *text, size_t n, uint8_t *out,
                                           size_t *count);
const char *stark_lab_uart_file_text(stark_lab_file_t why);

/* The log file names `uart_0001.log` (the app's directory scan): the number of a name of exactly
 * that shape, and the next free name after `highest` (STARK_ERR_NO_SPACE past 9999). Pure. */
bool stark_lab_uart_log_name_parse(const char *name, uint32_t *number);
stark_err_t stark_lab_uart_log_name_next(uint32_t highest, char *buf, size_t n);

/* CRC-32 (IEEE, the zlib one): start from 0, pass the result back to go on. Pure. */
uint32_t stark_lab_uart_crc32(uint32_t crc, const uint8_t *data, size_t n);

/* ---- the SPI probe (lab_spi.c; STARK-0706, ADR-0023 §6) ----------------------------------------
 *
 * One bounded, confirmed SPI transaction on a leased module slot, and nothing else. The probe runs
 * only on a slot whose module declares MOD_SPI and no function bit (a "SPI breakout"; MOD_IRQ may
 * be declared too): a slot declaring Sub-GHz, NFC, IR or any bit this firmware does not know
 * belongs to a driver and is refused, because a raw write to a CC1101 could set PATABLE or strobe
 * STX and walk around the transmit policy (ADR-0022 §6). The only chip select that can be asserted
 * is the slot's own, through stark_module's lease: the panel's and the card's devices on the same
 * bus are out of reach. No call repeats, streams or loops; no lease outlives a transaction.
 */
#define STARK_LAB_SPI_MAX      32u /* bytes of one transaction */
#define STARK_LAB_SPI_CLOCKS   6u  /* 100 kHz, 250 kHz, 500 kHz, 1 MHz, 2 MHz, 4 MHz */
#define STARK_LAB_SPI_NAME_MAX 18u /* a module name (module_desc.h: MODULE_NAME_MAX) */
#define STARK_LAB_SPI_WHY_MAX  24u
#define STARK_LAB_SPI_SLOTS    2u

typedef struct {
    uint8_t slot;
    bool eligible;
    char why[STARK_LAB_SPI_WHY_MAX];       /* the reason when not eligible: "no module", "owned by
                                            * Sub-GHz", ...; empty when eligible */
    char name[STARK_LAB_SPI_NAME_MAX + 1]; /* the module's name when one is known, else empty */
} stark_lab_spi_slot_t;

/* What the dialog shows and the send must match exactly: the service holds the fingerprint. */
typedef struct {
    uint8_t slot;
    uint32_t hz;
    uint8_t mode;
    uint8_t n;
    uint8_t tx[STARK_LAB_SPI_MAX];
} stark_lab_spi_req_t;

/* The clock list by index (0 past the end), as hertz. Pure. */
uint32_t stark_lab_spi_clock(size_t index);

/* "100 kHz", "250 kHz", "500 kHz", "1 MHz", "2 MHz", "4 MHz"; "?" for anything else. Pure. */
const char *stark_lab_spi_clock_text(uint32_t hz);

/*
 * Both slots as they are now, each with eligibility and the reason it has none: `no module`,
 * `generic module`, `unsupported module`, `conflict`, `owned by Sub-GHz` (or `NFC`, `IR`,
 * `unknown`), `no SPI declared`, `no chip select` (slot 1 has no MOD_CS). Returns how many were
 * written (at most STARK_LAB_SPI_SLOTS). Drives nothing. Any task.
 */
size_t stark_lab_spi_slots(stark_lab_spi_slot_t *out, size_t max);

/*
 * Records the transaction the app is about to ask a confirmation for: 1 to 32 bytes at one of the
 * six clocks and mode 0 to 3 on `slot`. STARK_ERR_INVALID_ARG for anything out of range,
 * STARK_ERR_NOT_SUPPORTED when the slot is not eligible (logged `lab: spi request refused: <why>`),
 * STARK_ERR_STATE before init. It replaces any earlier request and touches no bus. The request is
 * the arm: a fingerprint of the slot, its module, the clock, the mode and the bytes, which
 * stark_lab_spi_send() takes exactly once and only if the request still matches, within 30 s.
 */
stark_err_t stark_lab_spi_request(uint8_t slot, uint32_t hz, uint8_t mode, const uint8_t *tx,
                                  size_t n, stark_lab_spi_req_t *out);

/*
 * The app's OK on the dialog: makes the transaction recorded by stark_lab_spi_request() and puts
 * the `req->n` bytes that came back in `rx` (STARK_LAB_SPI_MAX bytes of room). The request is
 * consumed before the bus is touched, whatever happens. STARK_ERR_STATE for a request that is
 * missing, already used, cancelled, older than 30 s, changed (any field, or the slot's module) or
 * when the lab is not initialised; STARK_ERR_NOT_SUPPORTED when the slot is no longer eligible
 * (checked again here, and again by the transport); STARK_ERR_BUSY (a driver holds the slot: "slot
 * busy"), STARK_ERR_NOT_FOUND or the transport's error otherwise. The lease is claimed
 * ("spiprobe"), used and released inside the call, success or not. Logs `lab: spi slot 0 1 MHz mode
 * 0 tx 9f 00 00 00 rx 00 ef 40 18` and publishes STARK_LAB_EVT_SPI_DONE.
 */
stark_err_t stark_lab_spi_send(const stark_lab_spi_req_t *req, uint8_t *rx);

/* Forgets the request (the dialog's BACK, leaving the app). Any task. */
void stark_lab_spi_cancel(void);

/*
 * Recomputes the offer: a held pin that is no longer offered goes to hi-Z at once, loses its
 * owner and its arm, and is logged as `lab: pin <n> withdrawn (slot <s>)`; the offer is
 * logged again as `lab: pins ...` when it changed, and STARK_LAB_EVT_PINS is published.
 * The composition root calls it on STARK_SYS_MODULE; every call above also checks the live
 * table, so a pin is never used in the gap.
 */
void stark_lab_refresh(void);
