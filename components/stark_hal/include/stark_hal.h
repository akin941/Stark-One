/*
 * stark_hal.h — L1, thin port layer: time, GPIO, PWM.
 *
 * Deliberately small — it exists so L2 cores and host tests can be built
 * without ESP-IDF, not to abstract all of ESP-IDF (ARCHITECTURE.md §6.2).
 * No SPI wrapper: stark_board initialises SPI2 directly; devices attach
 * via esp_lcd / spi_bus_add_device in their own port files.
 *
 * Host-includable: no ESP-IDF headers here, so this header compiles
 * unchanged for both stark_hal_esp.c (target) and
 * test/host/support/stark_hal_host.c (host tests). ESP-IDF error
 * translation (stark_err_from_esp()) is NOT here — it takes esp_err_t,
 * which would break that host-includability, so it lives in the
 * ESP-IDF-only stark_hal_esp.h instead.
 *
 * GPIO pin numbers are never defined here. Callers get them from
 * stark_board_pins() (AGENTS.md: stark_board is the only pin-number
 * source of truth) and pass them in as plain ints.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "stark_err.h"

/* ---- Time ----------------------------------------------------------- */

/* Microseconds since boot. Target: esp_timer_get_time(). Host: a fake
 * counter that only advances when a test calls hal_host_set_now_us()
 * (test/host/support/stark_hal_host_control.h) — host tests never sleep. */
uint64_t stark_hal_now_us(void);

/* Blocking delay. Target: vTaskDelay(). Host: no-op (tests never sleep). */
void stark_hal_delay_ms(uint32_t ms);

/* Busy-wait delay for short waits (STARK-0702: a pulse under 1 ms). Target: esp_rom_delay_us().
 * Host: records the time and never waits. Not for more than 1 ms: use stark_hal_delay_ms(). */
void stark_hal_delay_us(uint32_t us);

/* ---- GPIO -------------------------------------------------------------
 *
 * A pin outside 0-48 (ESP32-S3's real GPIO range) is rejected with
 * STARK_ERR_INVALID_ARG by config_input()/config_output() on both
 * implementations (verified against ESP-IDF's own gpio_config() range
 * check, which returns the same code — this is not a host-only
 * invention). read()/write() have no error channel (bool/void); both
 * implementations degrade safely for an out-of-range pin instead
 * (read() returns false, write() is a no-op) rather than only being
 * "safe" on one side.
 *
 * stark_hal_gpio_read() after stark_hal_gpio_config_input(pin, pullup):
 * if nothing external drives the pin, the read reflects the pull-up/
 * pull-down setting (true if pullup, false otherwise) — this is standard
 * MCU pull resistor behaviour, not a HAL invention, and the fake models
 * it the same way (it has no concept of an external driver, so it always
 * reflects the pull setting; the real target only matches this when the
 * pin genuinely is undriven — an actively-driven pin always wins on
 * hardware, which the fake cannot represent).
 */

stark_err_t stark_hal_gpio_config_input(int pin, bool pullup);
stark_err_t stark_hal_gpio_config_output(int pin, bool initial);
bool stark_hal_gpio_read(int pin);
void stark_hal_gpio_write(int pin, bool level);

/* ---- Lab pads (STARK-0701, ADR-0023 §3) ---------------------------------------
 *
 * Pad calls for the lab transport (stark_lab_transport). Like the calls above they
 * check range only (STARK_ERR_INVALID_ARG outside 0-48); which pins a lab tool may
 * touch is stark_board's decision (stark_board_pin_class()), made by the callers.
 */
typedef enum { STARK_HAL_PULL_NONE, STARK_HAL_PULL_UP, STARK_HAL_PULL_DOWN } stark_hal_pull_t;

/* Input with the input buffer on, interrupts off and the chosen internal pull (about 45 kOhm). */
stark_err_t stark_hal_gpio_config_in(int pin, stark_hal_pull_t pull);

/*
 * Output at the weakest drive strength (GPIO_DRIVE_CAP_0, about 5 mA), no pull, the input
 * buffer left on so stark_hal_gpio_read() reads the pad back. The level is set before the
 * output is enabled. STARK_ERR_IO, with the pad released, when the drive strength cannot be
 * confirmed.
 */
stark_err_t stark_hal_gpio_config_out_weak(int pin, bool initial);

/* The pad is released: input, no pull, interrupts off. */
stark_err_t stark_hal_gpio_hiz(int pin);

/*
 * The weakest drive strength (GPIO_DRIVE_CAP_0) for a pad that a peripheral drives (the lab's
 * PWM, which routes LEDC to the pad), confirmed by reading it back; STARK_ERR_IO when it cannot
 * be. A pad that was an output through stark_hal_gpio_config_out_weak() or a PWM output is
 * reset by the next stark_hal_gpio_config_in()/hiz(), whatever it was routed to.
 */
stark_err_t stark_hal_gpio_drive_weak(int pin);

/*
 * Edge capture (STARK-0703, ADR-0023 §4): a GPIO any-edge interrupt timestamps every edge on an
 * input pad with the CPU cycle counter, for `window_us`, then removes the interrupt again.
 *
 *   ts, cap   the ring the caller owns: the first `cap` stamps are stored (cap 0: only counted)
 *   *n        stamps stored, at most cap
 *   *seen     edges seen, counted up to cap + 1 (cap > 0) or STARK_HAL_EDGES_GATE_MAX (cap 0):
 *             the capture ends at once when that many have come, so a signal too fast for the
 *             ring cannot hold the CPU in the interrupt; seen > cap means the ring overflowed
 *   *first_level  the pad's level just before the interrupt was enabled
 *
 * The pad is left as it was (an input with its pull). A cancel (stark_hal_gpio_edges_cancel(),
 * any task, no lock) ends the wait within 10 ms and returns STARK_ERR_STATE; the interrupt is
 * removed on every way out. STARK_ERR_INVALID_ARG for a pin outside 0-48, a NULL output, `cap`
 * above 0 with no ring, or a window of 0 or more than 4.1 s. Callers are serialised. The target
 * interrupt handler is in IRAM; the host fake returns what hal_host_edges_set() was given.
 */
#define STARK_HAL_EDGES_GATE_MAX 4096u
stark_err_t stark_hal_gpio_edges(int pin, uint32_t window_us, uint32_t *ts, size_t cap, size_t *n,
                                 uint32_t *seen, bool *first_level);
void stark_hal_gpio_edges_cancel(void);

/* The clock the stamps count at, in hertz (the CPU clock: 240 000 000 on the target). */
uint32_t stark_hal_tick_hz(void);

/* ---- PWM (LEDC) --------------------------------------------------------
 *
 * Channel allocation is caller-specified, not hardcoded here — but V0 has
 * exactly two PWM consumers, and this is their fixed assignment for as
 * long as V0 lasts:
 *
 *   channel 0 — buzzer   (components/stark_buzzer, not yet implemented)
 *   channel 1 — backlight (components/stark_display, not yet implemented)
 *   channel 2 — the lab's PWM output (stark_lab_transport, STARK-0702)
 *
 * A caller that needs a different channel is free to pass one; nothing
 * here enforces this table. It exists so two future components don't
 * pick the same channel independently.
 */

stark_err_t stark_hal_pwm_init(int pin, uint32_t hz, uint8_t channel);
stark_err_t stark_hal_pwm_set_freq(uint8_t channel, uint32_t hz);

/* pct > 100 is clamped to 100, not rejected — both implementations agree
 * on this; there is no invalid duty value a caller can pass. */
stark_err_t stark_hal_pwm_set_duty_pct(uint8_t channel, uint8_t pct);

/*
 * Stops one LEDC channel with the idle level low (STARK-0702: the lab's channel 2). Only that
 * channel is touched: the buzzer's and the backlight's keep running. The pad it drove is released
 * by stark_hal_gpio_hiz(). Channel 2 is the lab's: nothing else may use it.
 */
stark_err_t stark_hal_pwm_stop(uint8_t channel);
