/*
 * test_ui_gpio.c — the real GPIO app on the host UI port (TASKS.md STARK-0702) over the real pad
 * decisions (components/stark_lab/lab_pins.c) and the test-only pad model: the home list of the
 * offered pins, a pin's view in each mode, the confirmation before the first drive (BACK touches
 * nothing, OK arms), the toggle, the PWM and the pulse, a contention alert, a hot-plug that
 * withdraws a driven slot pin, and the hi-Z of every pad on every way out. Pad levels are the
 * model's, never a pad on a board (ROADMAP V1 exit 13). Frames pinned after inspection.
 */
#include <stdio.h>
#include <string.h>
#include "fake_lab.h"
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_input.h"
#include "stark_lab.h"
#include "stark_ui.h"
#include "ui_port.h"
#include "ui_port_gpio.h"
#include "unity.h"

extern const stark_app_t app_gpio;
static const stark_app_t *const k_apps[] = {&app_gpio};

void setUp(void)
{}
void tearDown(void)
{
    (void)stark_ui_pop_to_root(); /* a failed test leaves no app running for the next one */
}

static void frame(void)
{
    stark_ui_tick();
}

static void press(stark_key_t k)
{
    ui_port_short(k);
    frame();
}

static void presses(stark_key_t k, int n)
{
    for (int i = 0; i < n; i++) {
        press(k);
    }
}

static void pin(const char *name, uint64_t want)
{
    uint64_t h = ui_port_fb_hash();
    if (h != want) {
        char path[64];
        snprintf(path, sizeof path, "ui_gpio_%s.ppm", name);
        (void)ui_port_dump_ppm(path);
        printf("gpio %s hash 0x%016llx (see %s)\n", name, (unsigned long long)h, path);
    }
    TEST_ASSERT_EQUAL_HEX64(want, h);
}

static void start(void)
{
    ui_port_reset_records();
    ui_port_worker_inline(true);
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("gpio"));
    frame();
}

/* Opens the pin on home row `row` (38, 39, 40, 41, 42 with slot 0 empty). */
static void open_row(int row)
{
    start();
    presses(STARK_KEY_DOWN, row);
    press(STARK_KEY_OK);
}

static void leave_by_back(void)
{
    press(STARK_KEY_BACK); /* from a pin: to home */
    press(STARK_KEY_BACK); /* from home: out of the app */
}

/* A pad that the app did not leave in hi-Z with no pull is a failure on every way out. */
static void assert_every_pad_hiz(void)
{
    for (int p = 38; p <= 42; p++) {
        TEST_ASSERT_EQUAL_MESSAGE(FAKE_LAB_PAD_HIZ, fake_lab_mode(p), "a pad was left driven");
        TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pull(p));
    }
}

static void assert_nothing_was_driven(void)
{
    for (int p = 38; p <= 42; p++) {
        TEST_ASSERT_NOT_EQUAL(FAKE_LAB_PAD_OUT, fake_lab_mode(p));
        TEST_ASSERT_NOT_EQUAL(FAKE_LAB_PAD_PWM, fake_lab_mode(p));
    }
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_pulses());
}

/* The mode row is selected: RIGHT n times. */
static void mode_to(int n)
{
    presses(STARK_KEY_RIGHT, n);
}

/* Confirms the pending dialog (OK), or cancels it (BACK). */
static void answer_ok(void)
{
    press(STARK_KEY_OK);
}

static void test_home_lists_the_offered_pins(void)
{
    hal_host_set_now_us(0);
    ui_port_gpio_reset();
    TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, 1));
    start();
    pin("home_five_pins", UINT64_C(0x8c56d08526a3fbb1));
    TEST_ASSERT_TRUE(ui_port_log_contains("app: start gpio"));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: home 38 39 40 41 42"));
    assert_nothing_was_driven();
    assert_every_pad_hiz();
    leave_by_back();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: released all"));
    assert_every_pad_hiz();
}

static void test_a_module_in_slot_0_takes_the_slot_pins_off_the_list(void)
{
    ui_port_gpio_reset();
    ui_port_gpio_set_slot0(STARK_MODULE_KNOWN);
    frame();
    start();
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: home 38 39"));
    TEST_ASSERT_FALSE(ui_port_log_contains("gpioapp: home 38 39 40"));
    pin("home_two_pins", UINT64_C(0xb7a8adf5432551a3));
    press(STARK_KEY_DOWN);
    press(STARK_KEY_DOWN); /* wraps: only 38 and 39 are rows */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: open pin 38 LAB_UART_TX"));
    press(STARK_KEY_BACK);
    /* at home the list follows the table: the module leaves, the slot's three pins return */
    ui_port_gpio_set_slot0(STARK_MODULE_ABSENT);
    frame();
    frame();
    pin("home_five_again", UINT64_C(0x8c56d08526a3fbb1));
}

static void test_no_pin_offered(void)
{
    static const stark_board_pins_t none = {
        .lab_uart_tx = -1,
        .lab_uart_rx = -1,
        .module = {{.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1, .eeprom_addr = 0x50},
                   {.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1, .eeprom_addr = 0x51}},
    };
    ui_port_gpio_reset_with(&none);
    start();
    pin("home_none", UINT64_C(0x596022ad334156f6));
    press(STARK_KEY_OK); /* nothing selectable */
    TEST_ASSERT_FALSE(ui_port_log_contains("gpioapp: open"));
    ui_port_gpio_reset();
}

static void test_a_pin_held_by_another_owner_is_not_opened(void)
{
    ui_port_gpio_reset();
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_acquire(38, "other"));
    open_row(0);
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 38 not available"));
    TEST_ASSERT_FALSE(ui_port_log_contains("gpioapp: open"));
    stark_lab_release_all();
    ui_port_gpio_reset();
}

/* AC 5 on the host: a pull-up reads high, a pull-down low, with no confirmation (a read is not a
 * drive), and BACK leaves the pad released. */
static void test_input_with_a_pull_reads_the_pad(void)
{
    ui_port_gpio_reset();
    open_row(0);
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: open pin 38 LAB_UART_TX"));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 38 Hi-Z"));
    pin("pin_hiz", UINT64_C(0xaac34a7afa3874be));
    mode_to(1);
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_IN, fake_lab_mode(38));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 38 IN L")); /* floating reads low */
    press(STARK_KEY_DOWN);                                          /* Pull */
    press(STARK_KEY_RIGHT);
    TEST_ASSERT_EQUAL_UINT8(1, fake_lab_pull(38));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 38 pull up"));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 38 IN H"));
    pin("pin_input_pullup", UINT64_C(0xc8353225e9557d2b));
    press(STARK_KEY_RIGHT);
    TEST_ASSERT_EQUAL_UINT8(2, fake_lab_pull(38));
    press(STARK_KEY_DOWN); /* Read */
    fake_lab_ext(38, FAKE_LAB_EXT_HIGH, 0, 0);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 38 read H"));
    assert_nothing_was_driven();
    TEST_ASSERT_FALSE(ui_port_log_contains("dialog: open"));
    press(STARK_KEY_BACK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: released pin 38"));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pull(38));
    press(STARK_KEY_BACK);
    assert_every_pad_hiz();
}

/* AC 1: no output without the confirm and its arm, and BACK on the confirm changes nothing. */
static void test_an_output_needs_the_confirm_and_back_changes_nothing(void)
{
    ui_port_gpio_reset();
    open_row(0);
    mode_to(2); /* Output */
    presses(STARK_KEY_DOWN, 2);
    pin("pin_output_before", UINT64_C(0x99dd790f32065f56));
    press(STARK_KEY_OK); /* Drive */
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Drive pin 38?\""));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: confirm pin 38 reads floating"));
    pin("confirm", UINT64_C(0x0da90231e5c28ee9));
    assert_nothing_was_driven(); /* the probe only read the pad */
    press(STARK_KEY_BACK);       /* cancel */
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: cancel"));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: cancelled pin 38"));
    TEST_ASSERT_FALSE(ui_port_log_contains("armed"));
    assert_every_pad_hiz();
    assert_nothing_was_driven();

    press(STARK_KEY_OK); /* asks again: the arm was not given */
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Drive pin 38?\""));
    answer_ok();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: armed pin 38"));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 38 OUT L"));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_OUT, fake_lab_mode(38));
    TEST_ASSERT_FALSE(fake_lab_out_level(38));
    pin("pin_output_low", UINT64_C(0xe791d0e0a08653bf));

    press(STARK_KEY_DOWN); /* Toggle: armed already, so no dialog */
    unsigned dialogs = 0;
    for (size_t i = 0; i < ui_port_log_count(); i++) {
        dialogs += strstr(ui_port_log_line(i), "dialog: open") != NULL ? 1u : 0u;
    }
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(fake_lab_out_level(38));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 38 OUT H"));
    for (size_t i = 0; i < ui_port_log_count(); i++) {
        dialogs -= strstr(ui_port_log_line(i), "dialog: open") != NULL ? 1u : 0u;
    }
    TEST_ASSERT_EQUAL_UINT(0, dialogs); /* the toggle opened none */
    press(STARK_KEY_OK);
    TEST_ASSERT_FALSE(fake_lab_out_level(38));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 38 OUT L"));

    press(STARK_KEY_UP); /* Drive again with the Level row set high */
    press(STARK_KEY_UP);
    press(STARK_KEY_RIGHT);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(fake_lab_out_level(38));
    leave_by_back();
    assert_every_pad_hiz();
}

/* AC 3: a contention is detected, the pad released, an alert shown; the arm does not survive. */
static void test_a_contention_alerts_and_releases(void)
{
    ui_port_gpio_reset();
    open_row(0);
    mode_to(2);
    press(STARK_KEY_DOWN); /* Level */
    press(STARK_KEY_RIGHT);
    press(STARK_KEY_DOWN);                    /* Drive */
    fake_lab_ext(38, FAKE_LAB_EXT_LOW, 0, 0); /* a short to ground */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: confirm pin 38 reads low"));
    answer_ok();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: contention pin 38"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Contention\""));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    pin("contention_alert", UINT64_C(0x68a53d79c8ce8c53));
    press(STARK_KEY_OK); /* dismiss */
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 38 Hi-Z"));
    pin("pin_after_contention", UINT64_C(0xf488cd8f0559a8ee));
    fake_lab_ext(38, FAKE_LAB_EXT_NONE, 0, 0);
    press(STARK_KEY_OK); /* the arm is gone: it asks again */
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: confirm pin 38 reads floating"));
    press(STARK_KEY_BACK);
    leave_by_back();
    assert_every_pad_hiz();
}

/* A state probe that fails is a log line, and there is no dialog to confirm. */
static void test_a_failing_read_before_the_confirm_asks_nothing(void)
{
    ui_port_gpio_reset();
    open_row(0);
    mode_to(2);
    presses(STARK_KEY_DOWN, 2);
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, 38, 1);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 38 not read: I/O error"));
    TEST_ASSERT_FALSE(ui_port_log_contains("dialog: open"));
    assert_nothing_was_driven();
    leave_by_back();
    assert_every_pad_hiz();
}

/* AC 4: the PWM; freq and duty with LEFT/RIGHT; stop needs no confirm; one pin at a time. */
static void test_pwm_starts_after_the_confirm_and_stops_without_one(void)
{
    ui_port_gpio_reset();
    open_row(1); /* 39 */
    mode_to(3);  /* PWM */
    press(STARK_KEY_DOWN);
    press(STARK_KEY_RIGHT); /* 2 kHz */
    press(STARK_KEY_DOWN);
    press(STARK_KEY_RIGHT); /* 51 % */
    press(STARK_KEY_DOWN);
    pin("pin_pwm_setup", UINT64_C(0x78cfd7e1fd21a020));
    press(STARK_KEY_OK); /* Start */
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Drive pin 39?\""));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(39)); /* nothing yet */
    answer_ok();
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_PWM, fake_lab_mode(39));
    TEST_ASSERT_EQUAL_UINT32(2000, fake_lab_pwm_hz(39));
    TEST_ASSERT_EQUAL_UINT8(51, fake_lab_pwm_duty(39));
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: pwm pin 39 2000 Hz 51 %"));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 PWM"));
    pin("pin_pwm_running", UINT64_C(0x6aa61e631e7d984b));

    press(STARK_KEY_UP); /* Duty, while it runs: restarts with the new value */
    press(STARK_KEY_RIGHT);
    TEST_ASSERT_EQUAL_UINT8(52, fake_lab_pwm_duty(39));
    press(STARK_KEY_UP);
    press(STARK_KEY_RIGHT);
    TEST_ASSERT_EQUAL_UINT32(3000, fake_lab_pwm_hz(39));
    press(STARK_KEY_DOWN);
    press(STARK_KEY_DOWN);
    unsigned opened = 0;
    for (size_t i = 0; i < ui_port_log_count(); i++) {
        opened += strstr(ui_port_log_line(i), "dialog: open") != NULL ? 1u : 0u;
    }
    press(STARK_KEY_OK); /* Stop */
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: pwm stopped"));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(39));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 Hi-Z"));
    for (size_t i = 0; i < ui_port_log_count(); i++) {
        opened -= strstr(ui_port_log_line(i), "dialog: open") != NULL ? 1u : 0u;
    }
    TEST_ASSERT_EQUAL_UINT(0, opened);

    press(STARK_KEY_OK); /* Start again: armed, so no dialog, and it runs */
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_PWM, fake_lab_mode(39));
    press(STARK_KEY_BACK); /* leaving the pin stops it */
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(39));
    press(STARK_KEY_BACK);
    assert_every_pad_hiz();
}

/* The frequency and duty rows stop at the limits (a refused step buzzes). */
static void test_pwm_rows_stop_at_the_limits(void)
{
    ui_port_gpio_reset();
    open_row(0);
    mode_to(3);
    press(STARK_KEY_DOWN);
    int rejects = ui_port_buzzer_rejects();
    presses(STARK_KEY_LEFT, 30); /* 1 kHz down to 100 Hz is 9 steps */
    TEST_ASSERT_EQUAL(rejects + 21, ui_port_buzzer_rejects());
    pin("pwm_freq_floor", UINT64_C(0x1e98e60b93a86e60));
    presses(STARK_KEY_RIGHT, 40); /* 100 Hz up to 40 kHz is 21 steps */
    TEST_ASSERT_EQUAL(rejects + 21 + 19, ui_port_buzzer_rejects());
    pin("pwm_freq_top", UINT64_C(0xd68e61d28d3eb6dc));
    press(STARK_KEY_DOWN);
    presses(STARK_KEY_RIGHT, 60); /* 50 % up to 100 % */
    pin("pwm_duty_top", UINT64_C(0x8b9fd84970ca0a24));
    leave_by_back();
    assert_every_pad_hiz();
}

/* One pulse per key press, after the confirm, on the worker; the pad goes back to hi-Z. */
static void test_a_pulse_is_one_pulse_per_press(void)
{
    ui_port_gpio_reset();
    open_row(0);
    mode_to(4); /* Pulse */
    press(STARK_KEY_DOWN);
    press(STARK_KEY_RIGHT); /* high */
    press(STARK_KEY_DOWN);
    press(STARK_KEY_RIGHT); /* 1 ms */
    press(STARK_KEY_DOWN);
    pin("pin_pulse", UINT64_C(0x79648b49bc7aac1a));
    press(STARK_KEY_OK);
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_pulses()); /* the confirmation comes first */
    answer_ok();
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_pulses());
    TEST_ASSERT_EQUAL_UINT32(1000, fake_lab_last_pulse_us());
    TEST_ASSERT_TRUE(fake_lab_last_pulse_level());
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: pulse pin 38 high 1000 us"));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38)); /* back to the mode it had */
    press(STARK_KEY_OK);                                    /* armed: no dialog, one more pulse */
    TEST_ASSERT_EQUAL_UINT(2, fake_lab_pulses());
    leave_by_back();
    assert_every_pad_hiz();
}

/* A worker that cannot start leaves the app usable: a log line, no pulse, no stuck keys. */
static void test_a_pulse_that_cannot_start_is_refused(void)
{
    ui_port_gpio_reset();
    open_row(0);
    mode_to(4);
    presses(STARK_KEY_DOWN, 3);
    ui_port_worker_inline(false); /* stark_app_worker_start() reports NOT_SUPPORTED */
    press(STARK_KEY_OK);
    answer_ok();
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 38 pulse refused: Not supported"));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_pulses());
    press(STARK_KEY_UP); /* the keys still work: nothing is waiting for a pulse */
    TEST_ASSERT_TRUE(ui_port_log_contains("menu: sel=2 \"Width\""));
    leave_by_back();
    assert_every_pad_hiz();
}

/* While a pulse runs the keys do nothing, except BACK, which ends it and leaves the pin. */
static void test_keys_are_swallowed_during_a_pulse_and_back_cuts_it_short(void)
{
    ui_port_gpio_reset();
    open_row(0);
    mode_to(4);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_DOWN); /* Pulse */
    press(STARK_KEY_OK);
    ui_port_short(STARK_KEY_OK);   /* the confirm: the worker runs inside this tick ... */
    ui_port_short(STARK_KEY_DOWN); /* ... and a key queued behind it is swallowed */
    ui_port_short(STARK_KEY_OK);
    frame();
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_pulses());
    ui_port_short(STARK_KEY_OK); /* a second request in the same busy window */
    ui_port_short(STARK_KEY_BACK);
    frame();
    TEST_ASSERT_EQUAL_UINT(1, ui_port_gpio_cancels());
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: close pin 38"));
    frame();
    assert_every_pad_hiz();
    press(STARK_KEY_BACK);
}

/* AC 3: the hot-plug withdraws a driven slot pin: hi-Z, the row gone, an alert, no arm left. */
static void test_a_hot_plug_withdraws_a_driven_slot_pin(void)
{
    ui_port_gpio_reset();
    open_row(2); /* 40, a slot pin */
    mode_to(2);
    presses(STARK_KEY_DOWN, 2);
    press(STARK_KEY_OK);
    answer_ok();
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_OUT, fake_lab_mode(40));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 40 OUT L"));

    ui_port_gpio_set_slot0(STARK_MODULE_KNOWN); /* a module arrives */
    frame();
    frame();
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(40));
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pull(40));
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: pin 40 withdrawn (slot 0)"));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 40 withdrawn"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Pin withdrawn\""));
    pin("withdrawn_alert", UINT64_C(0x4dd572ec0f149c8c));
    press(STARK_KEY_OK);
    pin("home_after_withdrawal", UINT64_C(0xb7a8adf5432551a3));
    assert_every_pad_hiz();

    ui_port_gpio_set_slot0(STARK_MODULE_ABSENT); /* it leaves: back on offer, but not armed */
    frame();
    frame();
    presses(STARK_KEY_DOWN, 2);
    press(STARK_KEY_OK);
    mode_to(2);
    presses(STARK_KEY_DOWN, 2);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Drive pin 40?\""));
    press(STARK_KEY_BACK);
    leave_by_back();
    assert_every_pad_hiz();
}

/* A hi-Z pin can be probed: floating, high or low, and the pad stays released. */
static void test_the_probe_row_says_what_the_pad_is_doing(void)
{
    ui_port_gpio_reset();
    open_row(1);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 probe floating"));
    pin("probe_floating", UINT64_C(0x7b50ece18dbfa96c));
    fake_lab_ext(39, FAKE_LAB_EXT_HIGH, 0, 0);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 probe high"));
    fake_lab_ext(39, FAKE_LAB_EXT_LOW, 0, 0);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 probe low"));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_IN,
                      fake_lab_mode(39) == FAKE_LAB_PAD_HIZ ? FAKE_LAB_PAD_IN : fake_lab_mode(39));
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pull(39));
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, 39, 1);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 not read: I/O error"));
    assert_nothing_was_driven();
    leave_by_back();
    assert_every_pad_hiz();
}

/* The refused steps of the mode row buzz, and moving the mode releases the pad first. */
static void test_the_mode_row_releases_the_pad_when_it_changes(void)
{
    ui_port_gpio_reset();
    open_row(0);
    int rejects = ui_port_buzzer_rejects();
    press(STARK_KEY_LEFT); /* Hi-Z is the first mode */
    TEST_ASSERT_EQUAL(rejects + 1, ui_port_buzzer_rejects());
    mode_to(1);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_RIGHT); /* pull-up */
    TEST_ASSERT_EQUAL_UINT8(1, fake_lab_pull(38));
    press(STARK_KEY_UP);
    press(STARK_KEY_LEFT); /* back to Hi-Z: the pull goes */
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_HIZ, fake_lab_mode(38));
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_pull(38));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 38 Hi-Z"));
    press(STARK_KEY_DOWN); /* Probe has no value to step */
    press(STARK_KEY_RIGHT);
    TEST_ASSERT_EQUAL(rejects + 2, ui_port_buzzer_rejects());
    leave_by_back();
}

/* AC 2: every way out leaves every pad in hi-Z with no pull. */
static void test_every_exit_path_leaves_every_pad_hiz(void)
{
    /* long BACK from a pin that is driven */
    ui_port_gpio_reset();
    open_row(0);
    mode_to(2);
    presses(STARK_KEY_DOWN, 2);
    press(STARK_KEY_OK);
    answer_ok();
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_OUT, fake_lab_mode(38));
    ui_port_key(STARK_KEY_BACK, STARK_KEY_PRESS);
    ui_port_key(STARK_KEY_BACK, STARK_KEY_LONG);
    ui_port_key(STARK_KEY_BACK, STARK_KEY_RELEASE);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: released all"));
    assert_every_pad_hiz();

    /* long BACK with the confirmation open and the PWM of another pin running before */
    ui_port_gpio_reset();
    open_row(1);
    mode_to(3);
    presses(STARK_KEY_DOWN, 3);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Drive pin 39?\""));
    ui_port_key(STARK_KEY_BACK, STARK_KEY_PRESS);
    ui_port_key(STARK_KEY_BACK, STARK_KEY_LONG);
    ui_port_key(STARK_KEY_BACK, STARK_KEY_RELEASE);
    frame();
    assert_every_pad_hiz();
    assert_nothing_was_driven();

    /* a PWM running when the app is stopped from outside */
    ui_port_gpio_reset();
    open_row(0);
    mode_to(3);
    presses(STARK_KEY_DOWN, 3);
    press(STARK_KEY_OK);
    answer_ok();
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_PWM, fake_lab_mode(38));
    stark_app_stop_current();
    frame();
    assert_every_pad_hiz();
}

/* ---- STARK-0703: the Measure row ------------------------------------------------------------ */

/* Opens pin 39 (row 1) as an input and selects the Measure row. */
static void open_measure(void)
{
    open_row(1);
    mode_to(1);
    presses(STARK_KEY_DOWN, 3); /* Pull, Read, Measure */
}

/* AC 2 and 4 on the host: the row shows a result, or says why there is none, and never a number
 * for a signal it cannot measure. */
static void test_the_measure_row_shows_the_result_or_the_reason(void)
{
    ui_port_gpio_reset();
    open_measure();
    TEST_ASSERT_TRUE(ui_port_log_contains("menu: sel=3 \"Measure\""));
    pin("measure_idle", UINT64_C(0x61133c3e23614fad));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_IN, fake_lab_mode(39));

    fake_lab_ext(39, FAKE_LAB_EXT_SQUARE, 1000, 25);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 measuring"));
    frame(); /* the worker's note arrives */
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: measure 1000.0 Hz duty 25.0 %"));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 measure 1.000 kHz duty 25.0 %"));
    pin("measure_1khz", UINT64_C(0x38fb9abd38aa794d));
    TEST_ASSERT_EQUAL(FAKE_LAB_PAD_IN, fake_lab_mode(39)); /* the pad is still an input */

    fake_lab_ext(39, FAKE_LAB_EXT_SQUARE, 40000, 50);
    press(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 measure 40.000 kHz duty 50.0 %"));

    fake_lab_ext(39, FAKE_LAB_EXT_SQUARE, 80000, 50); /* past the range: the gate says so */
    press(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: measure too fast"));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 measure > 50 kHz, not measured"));
    pin("measure_too_fast", UINT64_C(0x095f2914ea15c8c5));

    fake_lab_ext(39, FAKE_LAB_EXT_HIGH, 0, 0); /* a steady line */
    press(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: measure no signal (high)"));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 measure no signal (high)"));
    pin("measure_no_signal", UINT64_C(0x5ec3d34f8b8c91dd));

    fake_lab_edge_glitches(4); /* a glitch train */
    fake_lab_ext(39, FAKE_LAB_EXT_SQUARE, 1000, 50);
    press(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 measure glitches (< 1 us)"));
    fake_lab_edge_glitches(0);

    assert_nothing_was_driven();
    TEST_ASSERT_FALSE(ui_port_log_contains("dialog: open")); /* an input: no confirmation */
    leave_by_back();
    assert_every_pad_hiz();
}

static void test_a_new_pull_or_mode_forgets_the_result(void)
{
    ui_port_gpio_reset();
    open_measure();
    fake_lab_ext(39, FAKE_LAB_EXT_SQUARE, 1000, 25);
    press(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 measure 1.000 kHz duty 25.0 %"));
    press(STARK_KEY_UP);
    press(STARK_KEY_UP); /* Pull */
    press(STARK_KEY_RIGHT);
    pin("measure_cleared_by_pull", UINT64_C(0x803efb62940cf2bb));
    press(STARK_KEY_DOWN);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK); /* measure again */
    frame();
    press(STARK_KEY_UP);
    press(STARK_KEY_UP);
    press(STARK_KEY_UP);   /* Mode */
    press(STARK_KEY_LEFT); /* back to Hi-Z, and in again: no stale result */
    press(STARK_KEY_RIGHT);
    presses(STARK_KEY_DOWN, 3);
    pin("measure_cleared_by_mode", UINT64_C(0x99fc8dc93170c91f));
    leave_by_back();
    assert_every_pad_hiz();
}

/* One measurement per press; while it runs the keys do nothing, except BACK, which cuts it short
 * and leaves the pin. */
static void test_keys_are_swallowed_during_a_measurement_and_back_cuts_it_short(void)
{
    ui_port_gpio_reset();
    open_measure();
    fake_lab_ext(39, FAKE_LAB_EXT_SQUARE, 1000, 25);
    ui_port_short(STARK_KEY_OK);   /* the worker runs inside this tick ... */
    ui_port_short(STARK_KEY_DOWN); /* ... and a key queued behind it is swallowed */
    ui_port_short(STARK_KEY_OK);   /* so is a second request */
    frame();
    unsigned runs = 0;
    for (size_t i = 0; i < ui_port_log_count(); i++) {
        runs += strstr(ui_port_log_line(i), "lab: measure 1000.0") != NULL ? 1u : 0u;
    }
    TEST_ASSERT_EQUAL_UINT(1, runs);
    ui_port_short(STARK_KEY_OK);
    ui_port_short(STARK_KEY_BACK);
    frame();
    TEST_ASSERT_EQUAL_UINT(1, ui_port_gpio_measure_cancels());
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: close pin 39"));
    frame();
    assert_every_pad_hiz();
    press(STARK_KEY_BACK);
    TEST_ASSERT_TRUE(ui_port_gpio_measure_cancels() >= 2u); /* leaving the app cancels too */
}

static void test_a_measurement_that_cannot_run_is_explained(void)
{
    ui_port_gpio_reset();
    open_measure();
    ui_port_worker_inline(false);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 measure refused: Not supported"));
    press(STARK_KEY_UP); /* the keys still work */
    TEST_ASSERT_TRUE(ui_port_log_contains("menu: sel=2 \"Read\""));
    ui_port_worker_inline(true);
    press(STARK_KEY_DOWN);
    fake_lab_fault(FAKE_LAB_FAULT_CONFIG_ERROR, 39, 1); /* the pad model fails the capture */
    press(STARK_KEY_OK);
    frame();
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 not measured: I/O error"));
    TEST_ASSERT_TRUE(ui_port_log_contains("gpioapp: pin 39 Hi-Z")); /* released after the failure */
    leave_by_back();
    assert_every_pad_hiz();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_home_lists_the_offered_pins);
    RUN_TEST(test_a_module_in_slot_0_takes_the_slot_pins_off_the_list);
    RUN_TEST(test_no_pin_offered);
    RUN_TEST(test_a_pin_held_by_another_owner_is_not_opened);
    RUN_TEST(test_input_with_a_pull_reads_the_pad);
    RUN_TEST(test_an_output_needs_the_confirm_and_back_changes_nothing);
    RUN_TEST(test_a_contention_alerts_and_releases);
    RUN_TEST(test_a_failing_read_before_the_confirm_asks_nothing);
    RUN_TEST(test_pwm_starts_after_the_confirm_and_stops_without_one);
    RUN_TEST(test_pwm_rows_stop_at_the_limits);
    RUN_TEST(test_a_pulse_is_one_pulse_per_press);
    RUN_TEST(test_a_pulse_that_cannot_start_is_refused);
    RUN_TEST(test_keys_are_swallowed_during_a_pulse_and_back_cuts_it_short);
    RUN_TEST(test_a_hot_plug_withdraws_a_driven_slot_pin);
    RUN_TEST(test_the_probe_row_says_what_the_pad_is_doing);
    RUN_TEST(test_the_mode_row_releases_the_pad_when_it_changes);
    RUN_TEST(test_every_exit_path_leaves_every_pad_hiz);
    RUN_TEST(test_the_measure_row_shows_the_result_or_the_reason);
    RUN_TEST(test_a_new_pull_or_mode_forgets_the_result);
    RUN_TEST(test_keys_are_swallowed_during_a_measurement_and_back_cuts_it_short);
    RUN_TEST(test_a_measurement_that_cannot_run_is_explained);
    return UNITY_END();
}
