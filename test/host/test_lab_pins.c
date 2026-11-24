/*
 * test_lab_pins.c — the lab service's pad decisions (TASKS.md STARK-0701, ADR-0023 §3) over the
 * real module core and fake 24C02 (the slot states) and the test-only pad model (the wires).
 * Software only: it proves the rules, never a level on a pad (ROADMAP V1 exit 13).
 */
#include <string.h>
#include "fake_24c02.h"
#include "fake_lab.h"
#include "fake_module_catalog.h"
#include "lab_pins.h"
#include "module_core.h"
#include "unity.h"

#define A0 0x50
#define A1 0x51

/* HARDWARE.md §2's map: lab pins 38, 39 and slot 0's 40, 41, 42. */
static const stark_board_pins_t k_map = {
    .lab_uart_tx = 38,
    .lab_uart_rx = 39,
    .module = {{.cs = 40, .irq0 = 41, .irq1 = 42, .en = -1, .eeprom_addr = A0},
               {.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1, .eeprom_addr = A1}},
};
static const module_wiring_t k_wires[] = {{A0, 40, 41, 42}, {A1, -1, -1, -1}};

static module_core_t s_mod;
static lab_pins_t s_lab;

static struct {
    int withdrawn[8][2]; /* pin, slot */
    size_t n_withdrawn;
    int contention[8];
    size_t n_contention;
    uint32_t delayed_us;
    unsigned in_calls, read_calls;
    unsigned fail_in_at, fail_read_at, fail_hiz;
    unsigned pwm_stops, pwm_starts, pulses;
    bool fail_pwm_stop, fail_pulse;
} s_rec;

static stark_err_t mio_probe(void *ctx, uint8_t addr)
{
    (void)ctx;
    return fake_24c02_probe(addr);
}

static stark_err_t mio_read(void *ctx, uint8_t addr, uint8_t off, uint8_t *buf, size_t len)
{
    (void)ctx;
    return fake_24c02_read_at(addr, off, buf, len);
}

static const module_io_t k_mio = {mio_probe, mio_read, NULL, NULL, NULL};

static size_t io_list(void *ctx, stark_module_entry_t *out, size_t max)
{
    (void)ctx;
    return module_core_list(&s_mod, out, max);
}

static stark_err_t io_pad_in(void *ctx, int pin, uint8_t pull)
{
    (void)ctx;
    if (++s_rec.in_calls == s_rec.fail_in_at) {
        return STARK_ERR_IO;
    }
    return fake_lab_pad_in(pin, pull);
}

static stark_err_t io_pad_out(void *ctx, int pin, bool level)
{
    (void)ctx;
    return fake_lab_pad_out(pin, level);
}

static stark_err_t io_pad_read(void *ctx, int pin, bool *level)
{
    (void)ctx;
    if (++s_rec.read_calls == s_rec.fail_read_at) {
        return STARK_ERR_IO;
    }
    return fake_lab_pad_read(pin, level);
}

static stark_err_t io_pad_hiz(void *ctx, int pin)
{
    (void)ctx;
    if (s_rec.fail_hiz != 0) {
        return STARK_ERR_IO;
    }
    return fake_lab_pad_hiz(pin);
}

static stark_err_t io_pwm_start(void *ctx, int pin, uint32_t hz, uint8_t duty_pct)
{
    (void)ctx;
    s_rec.pwm_starts++;
    return fake_lab_pwm_start(pin, hz, duty_pct);
}

static stark_err_t io_pwm_stop(void *ctx, int pin)
{
    (void)ctx;
    s_rec.pwm_stops++;
    stark_err_t err = fake_lab_pad_hiz(pin); /* the transport releases the pad even on failure */
    return s_rec.fail_pwm_stop ? STARK_ERR_IO : err;
}

static stark_err_t io_pulse(void *ctx, int pin, bool level, uint32_t us)
{
    (void)ctx;
    s_rec.pulses++;
    return s_rec.fail_pulse ? STARK_ERR_IO : fake_lab_pulse(pin, level, us);
}

static uint64_t s_now_us;

static void io_delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    s_rec.delayed_us += us;
    s_now_us += us;
    fake_lab_time_us(s_now_us);
}

static void io_log(void *ctx, lab_log_t what, int a, int b)
{
    (void)ctx;
    if (what == LAB_LOG_WITHDRAWN) {
        s_rec.withdrawn[s_rec.n_withdrawn][0] = a;
        s_rec.withdrawn[s_rec.n_withdrawn][1] = b;
        s_rec.n_withdrawn++;
    } else {
        s_rec.contention[s_rec.n_contention++] = a;
    }
}

static void build(void)
{
    const lab_io_t io = {.list = io_list,
                         .pad_in = io_pad_in,
                         .pad_out = io_pad_out,
                         .pad_read = io_pad_read,
                         .pad_hiz = io_pad_hiz,
                         .pwm_start = io_pwm_start,
                         .pwm_stop = io_pwm_stop,
                         .pulse = io_pulse,
                         .delay_us = io_delay_us,
                         .log = io_log};
    lab_pins_init(&s_lab, &io, &k_map);
}

static void ticks(int n)
{
    for (int i = 0; i < n; i++) {
        (void)module_core_tick(&s_mod);
    }
}

static void fit(uint8_t addr, int id)
{
    fake_24c02_put(addr, k_fake_modules[id - 1].image, MODULE_DESC_SIZE);
}

void setUp(void)
{
    memset(&s_rec, 0, sizeof(s_rec));
    s_now_us = 0;
    fake_24c02_reset();
    fake_lab_reset();
    module_core_init(&s_mod, &k_mio, k_wires, 2);
    ticks(2);
    build();
}
void tearDown(void)
{}

static uint32_t mask(void)
{
    return lab_pins_offered_mask(&s_lab);
}

static stark_module_state_t slot0_state(void)
{
    stark_module_entry_t e[STARK_MODULE_ENTRIES];
    (void)module_core_list(&s_mod, e, STARK_MODULE_ENTRIES);
    return e[0].state;
}

static void test_init_builds_the_table_from_the_map(void)
{
    TEST_ASSERT_EQUAL_UINT(5, s_lab.n);
    TEST_ASSERT_EQUAL_HEX32(0x1F, mask()); /* slot 0 reads ABSENT: all five are on offer */
    stark_lab_pin_t list[8];
    TEST_ASSERT_EQUAL_UINT(5, lab_pins_list(&s_lab, list, 8));
    TEST_ASSERT_EQUAL(38, list[0].pin);
    TEST_ASSERT_EQUAL_STRING("LAB_UART_TX", list[0].name);
    TEST_ASSERT_EQUAL(STARK_PIN_LAB, list[0].cls);
    TEST_ASSERT_EQUAL(42, list[4].pin);
    TEST_ASSERT_EQUAL(STARK_PIN_SLOT, list[4].cls);
    TEST_ASSERT_TRUE(list[4].offered);
    TEST_ASSERT_FALSE(list[4].held);
    TEST_ASSERT_EQUAL_UINT(5, lab_pins_list(&s_lab, NULL, 0)); /* count only */
    TEST_ASSERT_EQUAL_UINT(5, lab_pins_list(&s_lab, list, 2)); /* truncated, still five */
}

/* AC 2: a slot pin is on offer only while its slot reads ABSENT, whatever else it reads. */
static void test_slot_pins_follow_the_slot_state(void)
{
    fit(A0, FAKE_MODULE_BLANK); /* an EEPROM answered, content unusable */
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_GENERIC, slot0_state());
    TEST_ASSERT_EQUAL_HEX32(0x03, mask());

    fake_24c02_remove(A0);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_ABSENT, slot0_state());
    TEST_ASSERT_EQUAL_HEX32(0x1F, mask());

    fit(A0, FAKE_MODULE_SPI_IRQ); /* KNOWN */
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, slot0_state());
    TEST_ASSERT_EQUAL_HEX32(0x03, mask());

    stark_module_lease_t lease; /* a lease on it changes nothing: it was withdrawn already */
    TEST_ASSERT_EQUAL(
        STARK_OK, module_core_claim(&s_mod, 0, STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ, "t", &lease));
    TEST_ASSERT_EQUAL_HEX32(0x03, mask());
    module_core_release(&s_mod, &lease);

    fake_24c02_remove(A0); /* CONFLICT: two modules that cannot share */
    fit(A0, FAKE_MODULE_NFC_CARRIER);
    fit(A1, FAKE_MODULE_NFC_RIVAL);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_CONFLICT, slot0_state());
    TEST_ASSERT_EQUAL_HEX32(0x03, mask());
}

/* AC 1, from the service: a number that is not offered is refused by every call, touches no
 * hardware, and the sweep covers -1 to 64 with slot 0 empty and with a module in it. */
static void sweep(void)
{
    stark_lab_pin_t list[8];
    (void)lab_pins_list(&s_lab, list, 8);
    for (int pin = -1; pin <= 64; pin++) {
        bool offered = false;
        for (int i = 0; i < 5; i++) {
            offered = offered || (list[i].pin == pin && list[i].offered);
        }
        if (offered) {
            continue;
        }
        bool level = false;
        stark_lab_pinstate_t st;
        unsigned before = fake_lab_calls();
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_acquire(&s_lab, pin, "x"));
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_arm(&s_lab, pin));
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_input(&s_lab, pin, STARK_LAB_PULL_UP));
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_read(&s_lab, pin, &level));
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_state(&s_lab, pin, &st));
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_write(&s_lab, pin, true));
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_toggle(&s_lab, pin));
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_hiz(&s_lab, pin));
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_pwm_start(&s_lab, pin, 1000, 50));
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_pulse(&s_lab, pin, true, 100));
        stark_lab_info_t info;
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_info(&s_lab, pin, &info));
        lab_pins_release(&s_lab, pin);
        TEST_ASSERT_EQUAL_UINT(before, fake_lab_calls());
        TEST_ASSERT_EQUAL_UINT(0, s_rec.pwm_starts + s_rec.pwm_stops + s_rec.pulses);
    }
}

static void test_sweep_with_slot_0_empty(void)
{
    sweep();
}

static void test_sweep_with_slot_0_fitted(void)
{
    fit(A0, FAKE_MODULE_SPI_IRQ);
    ticks(2);
    sweep();
    TEST_ASSERT_EQUAL_UINT(3,
                           s_rec.n_withdrawn); /* 40, 41, 42 were logged once, by the first call */
    TEST_ASSERT_EQUAL(40, s_rec.withdrawn[0][0]);
    TEST_ASSERT_EQUAL(0, s_rec.withdrawn[0][1]);
}

/* An offered pin that is not held is refused with STATE and touches nothing. */
static void test_an_unheld_pin_is_refused_untouched(void)
{
    bool level;
    stark_lab_pinstate_t st;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_arm(&s_lab, 38));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_input(&s_lab, 38, STARK_LAB_PULL_NONE));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_read(&s_lab, 38, &level));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_state(&s_lab, 38, &st));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_write(&s_lab, 38, true));
    stark_lab_info_t info;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_toggle(&s_lab, 38));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_hiz(&s_lab, 38));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_pwm_start(&s_lab, 38, 1000, 50));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_pulse(&s_lab, 38, true, 100));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_info(&s_lab, 38, &info));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_calls());
    TEST_ASSERT_EQUAL_UINT(0, s_rec.pwm_starts + s_rec.pwm_stops + s_rec.pulses);
}

static void test_acquire_busy_release(void)
{
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_pins_acquire(&s_lab, 38, NULL));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_pins_acquire(&s_lab, 38, ""));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_calls());
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio"));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_calls()); /* released to hi-Z first */
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, lab_pins_acquire(&s_lab, 38, "other"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 39, "uart"));
    stark_lab_pin_t list[8];
    (void)lab_pins_list(&s_lab, list, 8);
    TEST_ASSERT_TRUE(list[0].held);
    TEST_ASSERT_TRUE(list[1].held);
    TEST_ASSERT_FALSE(list[2].held);
    TEST_ASSERT_EQUAL_STRING("gpio", s_lab.hold[0].owner);
    lab_pins_release(&s_lab, 38);
    (void)lab_pins_list(&s_lab, list, 8);
    TEST_ASSERT_FALSE(list[0].held);
    unsigned calls = fake_lab_calls();
    lab_pins_release(&s_lab, 38); /* not held: a no-op */
    TEST_ASSERT_EQUAL_UINT(calls, fake_lab_calls());
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio"));
}

static void test_owner_is_copied_and_cut(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "an owner name longer than sixteen"));
    TEST_ASSERT_EQUAL_STRING("an owner name l", s_lab.hold[0].owner);
}

static void test_acquire_that_cannot_reach_hiz_does_not_hold(void)
{
    s_rec.fail_hiz = 1;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_acquire(&s_lab, 38, "gpio"));
    s_rec.fail_hiz = 0;
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio")); /* not left held */
}

/* AC 3: a drive is refused until armed, and the arm never survives release, release_all,
 * withdrawal or a failed drive. */
static void test_write_needs_the_arm(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio"));
    unsigned calls = fake_lab_calls();
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_write(&s_lab, 38, true));
    TEST_ASSERT_EQUAL_UINT(calls, fake_lab_calls()); /* nothing was driven */
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 38));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 38, true));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_OUT, fake_lab_mode(38));
    TEST_ASSERT_TRUE(fake_lab_out_level(38));
    TEST_ASSERT_EQUAL_UINT32(LAB_CONTENTION_US, s_rec.delayed_us);  /* the read-back waited 1 ms */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 38, false)); /* the arm stays while held */
    TEST_ASSERT_FALSE(fake_lab_out_level(38));
}

static void test_the_arm_does_not_survive_release(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 38));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 38, true));
    lab_pins_release(&s_lab, 38);
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38)); /* released to hi-Z, no pull */
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pull(38));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio"));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_write(&s_lab, 38, true));
}

static void test_the_arm_does_not_survive_release_all(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 41, "gpio"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 38));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 41));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 38, true));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 41, true));
    lab_pins_release_all(&s_lab);
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(41));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 41, "gpio"));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_write(&s_lab, 38, true));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_write(&s_lab, 41, true));
}

/* AC 3: contention ends in hi-Z, with an error, a log line and no arm. */
static void test_contention_ends_in_hiz(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 38));
    fake_lab_ext(38, FAKE_LAB_EXT_LOW, 0, 0); /* something outside holds the pad low */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_write(&s_lab, 38, true));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL_UINT(1, s_rec.n_contention);
    TEST_ASSERT_EQUAL(38, s_rec.contention[0]);
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_write(&s_lab, 38, false)); /* the arm is gone */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 38));
    TEST_ASSERT_EQUAL(STARK_OK,
                      lab_pins_write(&s_lab, 38, false)); /* the same level: no contention */
    fake_lab_ext(38, FAKE_LAB_EXT_NONE, 0, 0);
    fake_lab_fault(FAKE_LAB_FAULT_PAD_STUCK_LOW, 38, 1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO,
                      lab_pins_write(&s_lab, 38, true)); /* a pad that does not follow */
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL_UINT(2, s_rec.n_contention);
}

/* A transport error mid-sequence leaves the pin in hi-Z and disarmed. */
static void test_a_fault_mid_write_leaves_the_pin_hiz(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 38));
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, 38, 1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_write(&s_lab, 38, true));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_write(&s_lab, 38, true));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 38));
    s_rec.fail_read_at = s_rec.read_calls + 1; /* the read-back fails after a good drive */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_write(&s_lab, 38, true));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL_UINT(0, s_rec.n_contention); /* an error, not contention */
}

static void test_input_sets_the_pull_and_validates_it(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 39, "gpio"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_input(&s_lab, 39, STARK_LAB_PULL_UP));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_IN, fake_lab_mode(39));
    TEST_ASSERT_EQUAL_UINT8(1, fake_lab_pull(39));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_input(&s_lab, 39, STARK_LAB_PULL_DOWN));
    TEST_ASSERT_EQUAL_UINT8(2, fake_lab_pull(39));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_input(&s_lab, 39, STARK_LAB_PULL_NONE));
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pull(39));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_pins_input(&s_lab, 39, (stark_lab_pull_t)7));
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, 39, 1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_input(&s_lab, 39, STARK_LAB_PULL_UP));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(39));
}

static void test_read_reports_the_pad_and_fails_into_hiz(void)
{
    bool level = true;
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 39, "gpio"));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_pins_read(&s_lab, 39, NULL));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_read(&s_lab, 39, &level));
    TEST_ASSERT_FALSE(level); /* a floating pad reads low */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_input(&s_lab, 39, STARK_LAB_PULL_UP));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_read(&s_lab, 39, &level));
    TEST_ASSERT_TRUE(level);
    s_rec.fail_read_at = s_rec.read_calls + 1;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_read(&s_lab, 39, &level));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(39));
}

/* The state probe: each pull for 2 ms, then no pull. */
static void test_state_probe_classifies_the_pad(void)
{
    stark_lab_pinstate_t st = STARK_LAB_PIN_LOW;
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 39, "gpio"));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_pins_state(&s_lab, 39, NULL));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_state(&s_lab, 39, &st));
    TEST_ASSERT_EQUAL(STARK_LAB_PIN_FLOATING, st);
    TEST_ASSERT_EQUAL_UINT32(2 * LAB_PROBE_SETTLE_US, s_rec.delayed_us);
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_IN, fake_lab_mode(39)); /* left an input... */
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pull(39));         /* ... with no pull */
    fake_lab_ext(39, FAKE_LAB_EXT_HIGH, 0, 0);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_state(&s_lab, 39, &st));
    TEST_ASSERT_EQUAL(STARK_LAB_PIN_HIGH, st);
    fake_lab_ext(39, FAKE_LAB_EXT_LOW, 0, 0);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_state(&s_lab, 39, &st));
    TEST_ASSERT_EQUAL(STARK_LAB_PIN_LOW, st);
}

static void test_state_probe_on_a_pad_that_moves_is_an_error(void)
{
    /* 250 Hz, 50 %: high for 2 ms, low for 2 ms. Low after the first 2 ms, high after the next. */
    fake_lab_ext(39, FAKE_LAB_EXT_SQUARE, 250, 50);
    s_now_us = 100;
    fake_lab_time_us(s_now_us);
    stark_lab_pinstate_t st;
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 39, "gpio"));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_state(&s_lab, 39, &st));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(39));
}

static void test_state_probe_fails_into_hiz_at_every_step(void)
{
    stark_lab_pinstate_t st;
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 39, "gpio"));
    for (unsigned step = 1; step <= 3; step++) { /* the three pad_in calls */
        s_rec.in_calls = 0;
        s_rec.fail_in_at = step;
        TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_state(&s_lab, 39, &st));
        TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(39));
    }
    s_rec.fail_in_at = 0;
    for (unsigned step = 1; step <= 2; step++) { /* the two reads */
        s_rec.read_calls = 0;
        s_rec.fail_read_at = step;
        TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_state(&s_lab, 39, &st));
        TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(39));
    }
}

/* AC 2: a hot-plug withdraws the pin, drives it to hi-Z at once and says so. */
static void test_hotplug_withdraws_a_driven_pin(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 40, "gpio"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 40));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 40, true)); /* an output, high */
    TEST_ASSERT_FALSE(lab_pins_refresh(&s_lab));                   /* nothing changed yet */
    TEST_ASSERT_EQUAL_UINT(0, s_rec.n_withdrawn);

    fit(A0, FAKE_MODULE_SPI_IRQ); /* a module appears in slot 0 */
    ticks(2);
    TEST_ASSERT_TRUE(lab_pins_refresh(&s_lab));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(40)); /* released at once */
    TEST_ASSERT_FALSE(fake_lab_out_level(40));
    TEST_ASSERT_EQUAL_UINT(3, s_rec.n_withdrawn);
    TEST_ASSERT_EQUAL(40, s_rec.withdrawn[0][0]);
    TEST_ASSERT_EQUAL(0, s_rec.withdrawn[0][1]); /* "lab: pin 40 withdrawn (slot 0)" */
    TEST_ASSERT_EQUAL(41, s_rec.withdrawn[1][0]);
    TEST_ASSERT_EQUAL(42, s_rec.withdrawn[2][0]);
    TEST_ASSERT_EQUAL_HEX32(0x03, s_lab.offered);
    TEST_ASSERT_TRUE(s_lab.hold[0].held); /* the LAB pin is untouched */
    TEST_ASSERT_FALSE(s_lab.hold[2].held);

    unsigned calls = fake_lab_calls();
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_write(&s_lab, 40, true));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_acquire(&s_lab, 40, "gpio"));
    TEST_ASSERT_EQUAL_UINT(calls, fake_lab_calls());
    TEST_ASSERT_FALSE(lab_pins_refresh(&s_lab)); /* and nothing is announced twice */
    TEST_ASSERT_EQUAL_UINT(3, s_rec.n_withdrawn);

    fake_24c02_remove(A0); /* the module leaves: the pins are on offer again, with no arm */
    ticks(2);
    TEST_ASSERT_TRUE(lab_pins_refresh(&s_lab));
    TEST_ASSERT_EQUAL_HEX32(0x1F, s_lab.offered);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 40, "gpio"));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_write(&s_lab, 40, true));
}

/* The live check: a pin call made before the refresh ran never uses the pin. */
static void test_a_pin_call_finds_the_withdrawal_itself(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 42, "gpio"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 42));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 42, true));
    (void)lab_pins_take_changed(&s_lab);
    fit(A0, FAKE_MODULE_SPI_IRQ);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_write(&s_lab, 42, false));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(42));
    TEST_ASSERT_FALSE(fake_lab_out_level(42)); /* it was not driven low either */
    TEST_ASSERT_EQUAL_UINT(1, s_rec.n_withdrawn);
    TEST_ASSERT_EQUAL(42, s_rec.withdrawn[0][0]);
    TEST_ASSERT_TRUE(lab_pins_take_changed(&s_lab));
    TEST_ASSERT_FALSE(lab_pins_take_changed(&s_lab)); /* once */
    TEST_ASSERT_FALSE(s_lab.hold[4].held);
}

static void test_a_held_pin_is_withdrawn_by_acquire_and_release_still_reaches_hiz(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 41, "gpio"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 41));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 41, true));
    fit(A0, FAKE_MODULE_SPI_IRQ);
    ticks(2);
    lab_pins_release(&s_lab, 41); /* no offer check: releasing must reach the pad */
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(41));
    TEST_ASSERT_FALSE(s_lab.hold[3].held);

    fake_24c02_remove(A0);
    ticks(2);
    (void)lab_pins_refresh(&s_lab);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 40, "gpio"));
    fit(A0, FAKE_MODULE_SPI_IRQ);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      lab_pins_acquire(&s_lab, 40, "again")); /* held, lost */
    TEST_ASSERT_FALSE(s_lab.hold[2].held);
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(40));
}

static void test_refresh_with_nothing_to_do_is_quiet(void)
{
    TEST_ASSERT_FALSE(lab_pins_refresh(&s_lab));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_calls());
    TEST_ASSERT_EQUAL_UINT(0, s_rec.n_withdrawn);
}

/* An empty module table (the module service has not started) offers no slot line. */
static size_t io_list_empty(void *ctx, stark_module_entry_t *out, size_t max)
{
    (void)ctx;
    (void)out;
    (void)max;
    return 0;
}

static void test_an_empty_module_table_offers_only_the_lab_pins(void)
{
    s_lab.io.list = io_list_empty;
    TEST_ASSERT_EQUAL_HEX32(0x03, mask());
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_acquire(&s_lab, 40, "gpio"));
}

static size_t io_list_overlong(void *ctx, stark_module_entry_t *out, size_t max)
{
    (void)ctx;
    memset(out, 0, max * sizeof(*out));
    return max + 7; /* a list that claims more entries than fit: clamped, never read past */
}

static void test_a_table_that_reports_too_many_entries_is_clamped(void)
{
    s_lab.io.list = io_list_overlong;
    TEST_ASSERT_EQUAL_HEX32(0x03, mask()); /* must not read beyond the array (ASan) */
}

/* ---- STARK-0702: toggle, info, hiz, PWM and pulse ------------------------------------------ */

static void hold_armed(int pin)
{
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, pin, "gpio"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, pin));
}

static void test_info_follows_what_the_service_did(void)
{
    stark_lab_info_t i;
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio"));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_pins_info(&s_lab, 38, NULL));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_info(&s_lab, 38, &i));
    TEST_ASSERT_EQUAL(STARK_LAB_MODE_HIZ, i.mode);
    TEST_ASSERT_FALSE(i.armed);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_input(&s_lab, 38, STARK_LAB_PULL_UP));
    (void)lab_pins_info(&s_lab, 38, &i);
    TEST_ASSERT_EQUAL(STARK_LAB_MODE_IN, i.mode);
    TEST_ASSERT_EQUAL(STARK_LAB_PULL_UP, i.pull);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 38));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 38, true));
    (void)lab_pins_info(&s_lab, 38, &i);
    TEST_ASSERT_EQUAL(STARK_LAB_MODE_OUT, i.mode);
    TEST_ASSERT_TRUE(i.level);
    TEST_ASSERT_TRUE(i.armed);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pwm_start(&s_lab, 38, 2000, 30));
    (void)lab_pins_info(&s_lab, 38, &i);
    TEST_ASSERT_EQUAL(STARK_LAB_MODE_PWM, i.mode);
    TEST_ASSERT_EQUAL_UINT32(2000, i.hz);
    TEST_ASSERT_EQUAL_UINT32(30, i.duty_pct);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_hiz(&s_lab, 38));
    (void)lab_pins_info(&s_lab, 38, &i);
    TEST_ASSERT_EQUAL(STARK_LAB_MODE_HIZ, i.mode);
    TEST_ASSERT_EQUAL_UINT32(0, i.hz);
    TEST_ASSERT_TRUE(i.armed); /* the pad is released, the pin and its arm are kept */
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL_UINT(1, s_rec.pwm_stops); /* hiz stopped the PWM first */
}

static void test_hiz_failures_end_in_hiz_and_disarm(void)
{
    stark_lab_info_t i;
    hold_armed(38);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 38, true));
    s_rec.fail_hiz = 1;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_hiz(&s_lab, 38));
    s_rec.fail_hiz = 0;
    (void)lab_pins_info(&s_lab, 38, &i);
    TEST_ASSERT_EQUAL(STARK_LAB_MODE_HIZ, i.mode);
    TEST_ASSERT_FALSE(i.armed);
}

static void test_toggle_flips_an_armed_output_only(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio"));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_toggle(&s_lab, 38)); /* not armed */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 38));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_toggle(&s_lab, 38)); /* not an output yet */
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 38, true));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_toggle(&s_lab, 38));
    TEST_ASSERT_FALSE(fake_lab_out_level(38));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_toggle(&s_lab, 38));
    TEST_ASSERT_TRUE(fake_lab_out_level(38));
    TEST_ASSERT_EQUAL_UINT32(3 * LAB_CONTENTION_US, s_rec.delayed_us); /* each read back */
    fake_lab_ext(38, FAKE_LAB_EXT_HIGH, 0, 0); /* now low is held high from outside */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 38, true));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_toggle(&s_lab, 38));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL_UINT(1, s_rec.n_contention);
}

/* AC 4: one PWM at a time, ranges refused before the arm, the arm required. */
static void test_pwm_needs_valid_arguments_the_arm_and_one_pin(void)
{
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio"));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 39, "gpio"));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_pwm_start(&s_lab, 38, 1000, 50)); /* not armed */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_pins_pwm_start(&s_lab, 38, 99, 50));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_pins_pwm_start(&s_lab, 38, 40001, 50));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_pins_pwm_start(&s_lab, 38, 1000, 101));
    TEST_ASSERT_EQUAL_UINT(0, s_rec.pwm_starts); /* nothing reached the transport */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 38));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 39));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pwm_start(&s_lab, 38, 100, 0));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_PWM, fake_lab_mode(38));
    TEST_ASSERT_EQUAL_UINT32(100, fake_lab_pwm_hz(38));
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, lab_pins_pwm_start(&s_lab, 39, 1000, 50)); /* a second pin */
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(39));
    TEST_ASSERT_EQUAL_UINT(1, s_rec.pwm_starts);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pwm_start(&s_lab, 38, 40000, 100)); /* the same pin: new */
    TEST_ASSERT_EQUAL_UINT32(40000, fake_lab_pwm_hz(38));
    TEST_ASSERT_EQUAL_UINT8(100, fake_lab_pwm_duty(38));
    lab_pins_pwm_stop(&s_lab);
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL_UINT(1, s_rec.pwm_stops);
    lab_pins_pwm_stop(&s_lab); /* nothing running: nothing is called */
    TEST_ASSERT_EQUAL_UINT(1, s_rec.pwm_stops);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pwm_start(&s_lab, 39, 1000, 50)); /* the channel is free */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 38, true));         /* its arm stayed */
}

/* AC 2: every path that ends a pin ends its PWM and leaves the pad in hi-Z. */
static void test_every_exit_path_stops_the_pwm(void)
{
    hold_armed(38);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pwm_start(&s_lab, 38, 1000, 50));
    lab_pins_release(&s_lab, 38);
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL(-1, s_lab.pwm_idx);

    hold_armed(39);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pwm_start(&s_lab, 39, 1000, 50));
    lab_pins_release_all(&s_lab);
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(39));
    TEST_ASSERT_EQUAL(-1, s_lab.pwm_idx);

    hold_armed(40); /* a slot pin: the hot-plug withdraws it */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pwm_start(&s_lab, 40, 1000, 50));
    fit(A0, FAKE_MODULE_SPI_IRQ);
    ticks(2);
    TEST_ASSERT_TRUE(lab_pins_refresh(&s_lab));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(40));
    TEST_ASSERT_EQUAL(-1, s_lab.pwm_idx);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_pwm_start(&s_lab, 40, 1000, 50));
    TEST_ASSERT_EQUAL_UINT(3, s_rec.pwm_stops);
}

static void test_another_mode_on_the_pin_stops_the_pwm_first(void)
{
    stark_lab_pinstate_t st;
    bool level;
    hold_armed(38);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pwm_start(&s_lab, 38, 1000, 50));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_input(&s_lab, 38, STARK_LAB_PULL_UP));
    TEST_ASSERT_EQUAL(-1, s_lab.pwm_idx);
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_IN, fake_lab_mode(38));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pwm_start(&s_lab, 38, 1000, 50));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 38, true));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_OUT, fake_lab_mode(38));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pwm_start(&s_lab, 38, 1000, 50));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_state(&s_lab, 38, &st));
    TEST_ASSERT_EQUAL(-1, s_lab.pwm_idx);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pwm_start(&s_lab, 38, 1000, 50));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_read(&s_lab, 38, &level)); /* a read leaves it running */
    TEST_ASSERT_EQUAL(0, s_lab.pwm_idx);
    TEST_ASSERT_EQUAL_UINT(3, s_rec.pwm_stops);
}

static void test_a_failing_pwm_start_or_stop_ends_in_hiz_and_disarms(void)
{
    stark_lab_info_t i;
    hold_armed(38);
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, 38, 1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_pwm_start(&s_lab, 38, 1000, 50));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    (void)lab_pins_info(&s_lab, 38, &i);
    TEST_ASSERT_FALSE(i.armed);
    TEST_ASSERT_EQUAL(-1, s_lab.pwm_idx);

    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 38));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pwm_start(&s_lab, 38, 1000, 50));
    s_rec.fail_pwm_stop = true; /* the stop itself fails: the pin is still released and disarmed */
    lab_pins_pwm_stop(&s_lab);
    (void)lab_pins_info(&s_lab, 38, &i);
    TEST_ASSERT_FALSE(i.armed);
    TEST_ASSERT_EQUAL(STARK_LAB_MODE_HIZ, i.mode);
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL(-1, s_lab.pwm_idx);

    s_rec.fail_pwm_stop =
        false; /* and a failing stop inside another mode's change fails that call */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 38));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pwm_start(&s_lab, 38, 1000, 50));
    s_rec.fail_pwm_stop = true;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_pulse(&s_lab, 38, true, 100));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL_UINT(0, s_rec.pulses);
}

/* A pulse is one pulse, from any mode, and the pad goes back to the mode it had. */
static void test_a_pulse_returns_the_pad_to_its_mode(void)
{
    stark_lab_info_t i;
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 38, "gpio"));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_pins_pulse(&s_lab, 38, true, 99));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_pins_pulse(&s_lab, 38, true, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_pins_pulse(&s_lab, 38, true, 100)); /* not armed */
    TEST_ASSERT_EQUAL_UINT(0, s_rec.pulses);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 38));

    /* from hi-Z */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pulse(&s_lab, 38, true, 100));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_pulses());
    TEST_ASSERT_EQUAL_UINT32(100, fake_lab_last_pulse_us());
    TEST_ASSERT_TRUE(fake_lab_last_pulse_level());
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL_UINT32(LAB_CONTENTION_US, s_rec.delayed_us); /* the idle level, read back */

    /* from an input with a pull-up */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_input(&s_lab, 38, STARK_LAB_PULL_UP));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pulse(&s_lab, 38, false, 10));
    TEST_ASSERT_FALSE(fake_lab_last_pulse_level());
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_IN, fake_lab_mode(38));
    TEST_ASSERT_EQUAL_UINT8(1, fake_lab_pull(38));
    (void)lab_pins_info(&s_lab, 38, &i);
    TEST_ASSERT_EQUAL(STARK_LAB_MODE_IN, i.mode);
    TEST_ASSERT_EQUAL(STARK_LAB_PULL_UP, i.pull);

    /* from an output driven high */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 38, true));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pulse(&s_lab, 38, false, 1000000));
    TEST_ASSERT_EQUAL_UINT32(1000000, fake_lab_last_pulse_us());
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_OUT, fake_lab_mode(38));
    TEST_ASSERT_TRUE(fake_lab_out_level(38));
    TEST_ASSERT_EQUAL_UINT(3, fake_lab_pulses());

    /* from a running PWM: the PWM is stopped, not resumed */
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pwm_start(&s_lab, 38, 1000, 50));
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_pulse(&s_lab, 38, true, 100));
    TEST_ASSERT_EQUAL(-1, s_lab.pwm_idx);
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
}

/* AC 3: a contention before the pulse ends in hi-Z with an error; a transport failure does too,
 * and neither leaves the arm. */
static void test_a_pulse_in_contention_or_failure_ends_in_hiz(void)
{
    stark_lab_info_t i;
    hold_armed(38);
    fake_lab_ext(38, FAKE_LAB_EXT_HIGH, 0, 0); /* held high from outside: the idle low fails */
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_pulse(&s_lab, 38, true, 100));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_pulses()); /* the pulse never started */
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL_UINT(1, s_rec.n_contention);
    (void)lab_pins_info(&s_lab, 38, &i);
    TEST_ASSERT_FALSE(i.armed);
    fake_lab_ext(38, FAKE_LAB_EXT_NONE, 0, 0);

    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_arm(&s_lab, 38));
    s_rec.fail_pulse = true;
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_pins_pulse(&s_lab, 38, true, 100));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL_UINT(1, s_rec.n_contention); /* an error, not a contention */
    (void)lab_pins_info(&s_lab, 38, &i);
    TEST_ASSERT_FALSE(i.armed);
}

/* AC 3: the withdrawal of a slot pin that is an output at the time, as in the app. */
static void test_a_withdrawn_slot_pin_is_hiz_with_no_arm_left(void)
{
    stark_lab_info_t i;
    hold_armed(40);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_write(&s_lab, 40, true));
    fit(A0, FAKE_MODULE_SPI_IRQ);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_pins_info(&s_lab, 40, &i));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(40));
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pull(40));
    fake_24c02_remove(A0);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_OK, lab_pins_acquire(&s_lab, 40, "gpio"));
    (void)lab_pins_info(&s_lab, 40, &i);
    TEST_ASSERT_FALSE(i.armed);
    TEST_ASSERT_EQUAL(STARK_LAB_MODE_HIZ, i.mode);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_builds_the_table_from_the_map);
    RUN_TEST(test_slot_pins_follow_the_slot_state);
    RUN_TEST(test_sweep_with_slot_0_empty);
    RUN_TEST(test_sweep_with_slot_0_fitted);
    RUN_TEST(test_an_unheld_pin_is_refused_untouched);
    RUN_TEST(test_acquire_busy_release);
    RUN_TEST(test_owner_is_copied_and_cut);
    RUN_TEST(test_acquire_that_cannot_reach_hiz_does_not_hold);
    RUN_TEST(test_write_needs_the_arm);
    RUN_TEST(test_the_arm_does_not_survive_release);
    RUN_TEST(test_the_arm_does_not_survive_release_all);
    RUN_TEST(test_contention_ends_in_hiz);
    RUN_TEST(test_a_fault_mid_write_leaves_the_pin_hiz);
    RUN_TEST(test_input_sets_the_pull_and_validates_it);
    RUN_TEST(test_read_reports_the_pad_and_fails_into_hiz);
    RUN_TEST(test_state_probe_classifies_the_pad);
    RUN_TEST(test_state_probe_on_a_pad_that_moves_is_an_error);
    RUN_TEST(test_state_probe_fails_into_hiz_at_every_step);
    RUN_TEST(test_hotplug_withdraws_a_driven_pin);
    RUN_TEST(test_a_pin_call_finds_the_withdrawal_itself);
    RUN_TEST(test_a_held_pin_is_withdrawn_by_acquire_and_release_still_reaches_hiz);
    RUN_TEST(test_refresh_with_nothing_to_do_is_quiet);
    RUN_TEST(test_an_empty_module_table_offers_only_the_lab_pins);
    RUN_TEST(test_a_table_that_reports_too_many_entries_is_clamped);
    RUN_TEST(test_info_follows_what_the_service_did);
    RUN_TEST(test_hiz_failures_end_in_hiz_and_disarm);
    RUN_TEST(test_toggle_flips_an_armed_output_only);
    RUN_TEST(test_pwm_needs_valid_arguments_the_arm_and_one_pin);
    RUN_TEST(test_every_exit_path_stops_the_pwm);
    RUN_TEST(test_another_mode_on_the_pin_stops_the_pwm_first);
    RUN_TEST(test_a_failing_pwm_start_or_stop_ends_in_hiz_and_disarms);
    RUN_TEST(test_a_pulse_returns_the_pad_to_its_mode);
    RUN_TEST(test_a_pulse_in_contention_or_failure_ends_in_hiz);
    RUN_TEST(test_a_withdrawn_slot_pin_is_hiz_with_no_arm_left);
    return UNITY_END();
}
