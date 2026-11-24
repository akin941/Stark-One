/*
 * lab_transport_fake.c — test substitute for components/stark_lab_transport (ADR-0023 §2): the
 * same API, answered by the test-only pad model (test/fake/fake_lab.c) instead of the GPIO
 * matrix. Test image only. Like production it applies the shared pin guard
 * (stark_lab_pad_ok(), stark_lab_pad_hiz_ok()), so it can never touch a pin the policy
 * refuses; the pad model itself knows no policy.
 *
 * Scenarios steer the model through GDB pokes (scripts/test_emu.py POKE_VARS), each applied
 * before the next pad call after it changes:
 *   emu_lab_ext_pin   the pad an external source is on (default 38)
 *   emu_lab_ext_mode  0 none, 1 high, 2 low (a short to ground), 3 square wave
 *   emu_lab_ext_hz    ... its frequency and
 *   emu_lab_ext_duty  ... duty in percent, for mode 3
 *   emu_lab_wire      from * 64 + to: that pad's output (or PWM) is the external source of `to`
 *                     (0 removes the wire)
 *   emu_lab_fault     a fake_lab_fault_t on any pad, until it changes again (0 clears it)
 * With a wire in place a PWM start is followed by the model's own check: the destination pad is
 * sampled 1000 times over four periods and `lab: wire <from> -> <to> edges=<n> high=<pct>%`
 * is logged. The model's clock is the emulated clock, so a square wave runs in emulated time.
 * The edge capture (STARK-0703) stamps the edges of the pad's source (the external one, or a wire)
 * at 240 MHz from that clock, and then waits the window out in 10 ms slices like the hardware, so
 * a measurement takes the time it would; the tick is the model's, not the CPU's.
 * The I2C group (STARK-0704) is the same bus as everything else in this image: the five lab devices
 * of the model (0x23, 0x3C, 0x48, 0x68, 0x76), the module ID EEPROMs of the fake 24C02 transport at
 * 0x50 and 0x51 and the fake PN532 at 0x24 (both asked through their own transports, so one scan
 * shows one consistent bus and a NACK from either is a NACK here), behind the production header's
 * guard (stark_lab_i2c_args_ok(), stark_lab_i2c_allowed()). Pokes:
 *   emu_lab_i2c_devs   a bitmask over the five lab devices (0x1F at boot: all of them)
 *   emu_lab_i2c_fault  0 none, 1 NACK_ALL, 2 TIMEOUT at 0x48 (the sweep goes on without it), 3
 * STUCK (every call times out, which ends the sweep after three), 4 DROP_DEVICE at 0x68 (it answers
 * once and vanishes); until it changes again Integration evidence only: nothing here shows a level
 * on a pad (docs/VALIDATION.md §2).
 */
#include "esp_timer.h"
#include "fake_lab.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stark_lab_transport.h"
#include "stark_log.h"
#include "stark_module_transport.h"
#include "stark_nfc_transport.h"

volatile uint8_t emu_lab_ext_pin = 38;
volatile uint8_t emu_lab_ext_mode;
volatile uint32_t emu_lab_ext_hz = 1000;
volatile uint8_t emu_lab_ext_duty = 50;
volatile uint16_t emu_lab_wire;
volatile uint8_t emu_lab_fault;
volatile uint8_t emu_lab_i2c_devs = FAKE_LAB_I2C_ALL;
volatile uint8_t emu_lab_i2c_fault;

#define LAB_PROBE_MS 5u /* what the other fakes are asked with; they do not wait */

static bool s_open;
static volatile bool s_cancel;
static volatile bool s_edges_cancel;
static uint16_t s_ext_key = 0xFFFF; /* the ext pokes as last applied: pin, mode, hz, duty packed */
static uint32_t s_ext_hz_applied = 0xFFFFFFFFu;
static uint8_t s_ext_duty_applied = 0xFF;
static uint16_t s_wire_applied;
static uint8_t s_fault_applied;
static uint8_t s_i2c_devs_applied = FAKE_LAB_I2C_ALL;
static uint8_t s_i2c_fault_applied;

static void follow_pokes(void)
{
    fake_lab_time_us((uint64_t)esp_timer_get_time());
    uint16_t key = (uint16_t)(((uint16_t)emu_lab_ext_pin << 8) | emu_lab_ext_mode);
    uint32_t hz = emu_lab_ext_hz;
    uint8_t duty = emu_lab_ext_duty;
    if (key != s_ext_key || hz != s_ext_hz_applied || duty != s_ext_duty_applied) {
        if ((s_ext_key >> 8) != (key >> 8) && s_ext_key != 0xFFFF) {
            fake_lab_ext((int)(s_ext_key >> 8), FAKE_LAB_EXT_NONE, 0, 0); /* it moved pads */
        }
        fake_lab_ext((int)(key >> 8), (fake_lab_ext_t)(key & 0xFFu), hz, duty);
        s_ext_key = key;
        s_ext_hz_applied = hz;
        s_ext_duty_applied = duty;
    }
    uint16_t wire = emu_lab_wire;
    if (wire != s_wire_applied) {
        fake_lab_wire(-1, 0);
        if (wire != 0) {
            fake_lab_wire(wire / 64, wire % 64);
        }
        s_wire_applied = wire;
    }
    uint8_t fault = emu_lab_fault;
    if (fault != s_fault_applied && fault < FAKE_LAB_FAULT_COUNT) {
        fake_lab_fault((fake_lab_fault_t)fault, -1, fault == 0 ? 0 : -1);
        s_fault_applied = fault;
    }
}

/* The I2C pokes, applied before each bus call after they change. */
static void follow_i2c_pokes(void)
{
    uint8_t devs = emu_lab_i2c_devs;
    if (devs != s_i2c_devs_applied) {
        fake_lab_i2c_devices(devs);
        s_i2c_devs_applied = devs;
    }
    uint8_t fault = emu_lab_i2c_fault;
    if (fault != s_i2c_fault_applied && fault < FAKE_LAB_I2C_FAULT_COUNT) {
        switch ((fake_lab_i2c_fault_t)fault) {
            case FAKE_LAB_I2C_TIMEOUT:
                fake_lab_i2c_fault(FAKE_LAB_I2C_TIMEOUT, 0x48, -1);
                break;
            case FAKE_LAB_I2C_DROP_DEVICE:
                fake_lab_i2c_fault(FAKE_LAB_I2C_DROP_DEVICE, 0x68, -1);
                break;
            case FAKE_LAB_I2C_FAULT_NONE:
                fake_lab_i2c_fault(FAKE_LAB_I2C_FAULT_NONE, -1, 0);
                break;
            case FAKE_LAB_I2C_NACK_ALL:
            case FAKE_LAB_I2C_STUCK:
            case FAKE_LAB_I2C_FAULT_COUNT:
            default:
                fake_lab_i2c_fault((fake_lab_i2c_fault_t)fault, -1, -1);
                break;
        }
        s_i2c_fault_applied = fault;
    }
}

/* The module ID EEPROMs and the PN532 are the other fakes' (their transports), asked here. */
static stark_err_t i2c_ext_probe(uint8_t addr)
{
    if (addr == STARK_LAB_I2C_PN532) {
        stark_err_t err = stark_nfc_transport_present(LAB_PROBE_MS);
        return err == STARK_ERR_STATE ? STARK_ERR_NOT_FOUND : err; /* not open: nobody there */
    }
    if (addr >= 0x50u && addr <= 0x57u) {
        return stark_module_transport_probe(addr, LAB_PROBE_MS);
    }
    return STARK_ERR_NOT_FOUND;
}

static stark_err_t i2c_ext_xfer(uint8_t addr, const uint8_t *w, size_t wn, uint8_t *r, size_t rn)
{
    if (addr == STARK_LAB_I2C_PN532) {
        stark_err_t err = wn > 0u ? stark_nfc_transport_write(w, wn, LAB_PROBE_MS) : STARK_OK;
        if (err == STARK_OK && rn > 0u) {
            err = stark_nfc_transport_read(r, rn, LAB_PROBE_MS);
        }
        return err == STARK_ERR_STATE ? STARK_ERR_NOT_FOUND : err;
    }
    if (addr >= 0x50u && addr <= 0x57u) {
        /* The EEPROM transport reads only (ADR-0021 §2); a write never gets here (the guard). */
        if (wn == 1u && rn > 0u) {
            return stark_module_transport_read(addr, w[0], r, rn, LAB_PROBE_MS);
        }
        return STARK_ERR_NOT_SUPPORTED;
    }
    return STARK_ERR_NOT_FOUND;
}

/* The model's own check of a wire: the destination pad over four periods of the PWM output. */
static void check_wire(int from, uint32_t hz)
{
    int to = s_wire_applied % 64;
    if (s_wire_applied == 0 || (int)(s_wire_applied / 64) != from) {
        return;
    }
    uint64_t span = 4000000u / hz; /* four periods, in µs */
    unsigned edges = 0;
    unsigned high = 0;
    bool prev = false;
    for (unsigned i = 0; i < 1000; i++) {
        bool level = false;
        fake_lab_time_us(span * i / 1000u);
        (void)fake_lab_pad_read(to, &level);
        edges += (level && !prev) ? 1u : 0u;
        high += level ? 1u : 0u;
        prev = level;
    }
    STARK_LOGI("lab", "wire %d -> %d edges=%u high=%u%%", from, to, edges, high / 10u);
}

stark_err_t stark_lab_transport_open(void)
{
    if (!s_open) {
        fake_lab_reset();
        fake_lab_i2c_ext(i2c_ext_probe, i2c_ext_xfer);
        s_open = true;
    }
    return STARK_OK;
}

void stark_lab_transport_close(void)
{
    s_open = false;
}

stark_err_t stark_lab_transport_pad_in(int pin, uint8_t pull)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    follow_pokes();
    return fake_lab_pad_in(pin, pull);
}

stark_err_t stark_lab_transport_pad_out(int pin, bool level)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    follow_pokes();
    return fake_lab_pad_out(pin, level);
}

stark_err_t stark_lab_transport_pad_read(int pin, bool *level)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    follow_pokes();
    return fake_lab_pad_read(pin, level);
}

stark_err_t stark_lab_transport_pad_hiz(int pin)
{
    if (!stark_lab_pad_hiz_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    return fake_lab_pad_hiz(pin);
}

stark_err_t stark_lab_transport_pwm_start(int pin, uint32_t hz, uint8_t duty_pct)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (hz < STARK_LAB_PWM_HZ_MIN || hz > STARK_LAB_PWM_HZ_MAX || duty_pct > 100u) {
        return STARK_ERR_INVALID_ARG;
    }
    follow_pokes();
    stark_err_t err = fake_lab_pwm_start(pin, hz, duty_pct);
    if (err == STARK_OK) {
        check_wire(pin, hz);
    }
    return err;
}

stark_err_t stark_lab_transport_pwm_stop(int pin)
{
    if (!stark_lab_pad_hiz_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    return fake_lab_pad_hiz(pin);
}

/* The width is waited out like the hardware's, in slices that the cancel can cut short. */
stark_err_t stark_lab_transport_pulse(int pin, bool level, uint32_t us)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (us < STARK_LAB_PULSE_US_MIN || us > STARK_LAB_PULSE_US_MAX) {
        return STARK_ERR_INVALID_ARG;
    }
    follow_pokes();
    s_cancel = false;
    for (uint32_t left = us / 1000u; left > 0u && !s_cancel;) {
        uint32_t slice = left < 10u ? left : 10u;
        vTaskDelay(pdMS_TO_TICKS(slice));
        left -= slice;
    }
    return fake_lab_pulse(pin, level, us);
}

void stark_lab_transport_pulse_cancel(void)
{
    s_cancel = true;
}

stark_err_t stark_lab_transport_pad_edges(int pin, uint32_t window_us, uint32_t *ts, size_t cap,
                                          size_t *n, uint32_t *seen, bool *first_level)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (window_us < STARK_LAB_EDGES_WINDOW_US_MIN || window_us > STARK_LAB_EDGES_WINDOW_US_MAX) {
        return STARK_ERR_INVALID_ARG;
    }
    follow_pokes();
    s_edges_cancel = false;
    stark_err_t err = fake_lab_pad_edges(pin, window_us, ts, cap, n, seen, first_level);
    if (err != STARK_OK) {
        return err;
    }
    /* The stamps cover the window; the wait is what makes the measurement take its time. A fast
     * source ends the capture early on the hardware (the ring is full), so does it here. */
    uint32_t limit = cap > 0u ? (uint32_t)cap + 1u : STARK_LAB_EDGES_GATE_MAX;
    for (uint32_t left = window_us / 1000u; left > 0u && *seen < limit && !s_edges_cancel;) {
        uint32_t slice = left < 10u ? left : 10u;
        vTaskDelay(pdMS_TO_TICKS(slice));
        left -= slice;
    }
    return s_edges_cancel ? STARK_ERR_STATE : STARK_OK;
}

void stark_lab_transport_edges_cancel(void)
{
    s_edges_cancel = true;
}

uint32_t stark_lab_transport_tick_hz(void)
{
    return FAKE_LAB_TICK_HZ;
}

stark_err_t stark_lab_transport_i2c_probe(uint8_t addr, uint32_t timeout_ms)
{
    if (!stark_lab_i2c_args_ok(addr, 0u, 0u, 0u, timeout_ms)) {
        return STARK_ERR_INVALID_ARG;
    }
    follow_i2c_pokes();
    return fake_lab_i2c_probe(addr);
}

stark_err_t stark_lab_transport_i2c_xfer(uint8_t addr, uint32_t hz, const uint8_t *w, size_t wn,
                                         uint8_t *r, size_t rn, uint32_t timeout_ms)
{
    if (!stark_lab_i2c_args_ok(addr, wn, rn, hz, timeout_ms) || (wn > 0u && w == NULL) ||
        (rn > 0u && r == NULL)) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!stark_lab_i2c_allowed(stark_board_pins(), (stark_module_caps() & STARK_CAP_NFC) != 0u,
                               addr, wn, rn)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    follow_i2c_pokes();
    return fake_lab_i2c_xfer(addr, w, wn, r, rn);
}
