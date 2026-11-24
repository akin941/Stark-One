/*
 * fake_lab.h — the test-only model of the lab's wires (TASKS.md STARK-0701, ADR-0023 §2, §9).
 * Pads today (hi-Z, input with a pull, output with a level), each with an external source the
 * harness sets and wires between pads; the I2C bus, SPI devices and the UART peer join in the
 * tasks that need them. Spec-derived, never a recording of a device.
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
