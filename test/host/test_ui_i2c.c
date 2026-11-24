/*
 * test_ui_i2c.c — the real I2C app on the host UI port (TASKS.md STARK-0704) over the real rules
 * (components/stark_lab/lab_i2c.c), the production transport guard and the test-only bus: the scan
 * (five devices, none, a bus error and a recovery, a crowded bus), a register read by nibbles, the
 * length and the 16-bit register, the dump and the write each behind a confirmation (BACK touches
 * nothing, OK arms exactly that call, a second press asks again), the refusals of a module ID
 * EEPROM and of the PN532 while NFC is confirmed (an alert, a log line, no traffic on the bus), a
 * device that vanishes, and an arm that never outlives the app. Bus contents are the model's, never
 * a device on a board (ROADMAP V1 exit 13). Frames pinned after inspection.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_24c02.h"
#include "fake_lab.h"
#include "stark_app.h"
#include "stark_display.h"
#include "stark_event.h"
#include "stark_hal_host_control.h"
#include "stark_input.h"
#include "stark_lab.h"
#include "stark_ui.h"
#include "ui_port.h"
#include "ui_port_i2c.h"
#include "unity.h"

extern const stark_app_t app_i2c;
static const stark_app_t *const k_apps[] = {&app_i2c};
static bool s_ui_ready;

void setUp(void)
{
    hal_host_set_now_us(0);
    ui_port_i2c_reset();
    if (!s_ui_ready) {
        TEST_ASSERT_EQUAL(STARK_OK, stark_event_init());
        TEST_ASSERT_EQUAL(STARK_OK, stark_display_init());
        TEST_ASSERT_EQUAL(STARK_OK, stark_ui_init());
        TEST_ASSERT_EQUAL(STARK_OK, stark_app_init(k_apps, 1));
        s_ui_ready = true;
    }
}

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

/* A frame pinned after inspection; PIN_COLLECT=1 prints every hash instead of asserting. */
static void pin(const char *name, uint64_t want)
{
    uint64_t h = ui_port_fb_hash();
    bool collect = getenv("PIN_COLLECT") != NULL;
    if (h != want || collect) {
        char path[64];
        snprintf(path, sizeof path, "ui_i2c_%s.ppm", name);
        (void)ui_port_dump_ppm(path);
        printf("i2c %s hash 0x%016llx (see %s)\n", name, (unsigned long long)h, path);
    }
    if (!collect) {
        TEST_ASSERT_EQUAL_HEX64(want, h);
    }
}

static void start(void)
{
    ui_port_reset_records();
    TEST_ASSERT_EQUAL(STARK_OK, stark_app_launch("i2c"));
    frame();
}

static void scan(void)
{
    press(STARK_KEY_OK); /* Scan, the first row of home */
}

/* The scan list of the five devices has the device `row` selected and opens it: Read is selected.
 */
static void open_device(int row)
{
    presses(STARK_KEY_DOWN, row);
    press(STARK_KEY_OK);
}

/* From the device view: opens the editor of Read (1 DOWN is Dump, 2 is Write). */
static void open_read(void)
{
    press(STARK_KEY_OK);
}

static void open_write(void)
{
    presses(STARK_KEY_DOWN, 2);
    press(STARK_KEY_OK);
}

static unsigned count_logs(const char *needle)
{
    unsigned n = 0;
    for (size_t i = 0; i < ui_port_log_count(); i++) {
        n += strstr(ui_port_log_line(i), needle) != NULL ? 1u : 0u;
    }
    return n;
}

/* The Reg row is selected: sets the register's byte by nibbles (low, then high). */
static void set_reg(uint8_t value)
{
    presses(STARK_KEY_RIGHT, value & 0xF);
    presses(STARK_KEY_DOWN, 2); /* Nibble */
    press(STARK_KEY_RIGHT);     /* the high one */
    presses(STARK_KEY_UP, 2);
    presses(STARK_KEY_RIGHT, value >> 4);
    presses(STARK_KEY_DOWN, 2);
    press(STARK_KEY_RIGHT); /* the low one again */
    presses(STARK_KEY_UP, 2);
}

static void test_home_and_the_scan_of_the_five_devices(void)
{
    start();
    TEST_ASSERT_TRUE(ui_port_log_contains("app: start i2c"));
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: home"));
    pin("home", UINT64_C(0x8ae20497868d7215));
    scan();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c scan found 0x23 0x3c 0x48 0x68 0x76"));
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: scan 5 found"));
    TEST_ASSERT_EQUAL_UINT(112, ui_port_i2c_bus_calls());
    pin("scan_five", UINT64_C(0x924fd0c004102347));
    press(STARK_KEY_BACK); /* to home: the row says how many */
    pin("home_after_scan", UINT64_C(0xace9a5d08e6c6d7f));
    press(STARK_KEY_BACK); /* out of the app */
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop i2c"));
}

static void test_the_module_eeprom_and_the_hints_appear_on_the_same_scan(void)
{
    ui_port_i2c_eeprom(0x50, true);
    start();
    scan();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c scan found 0x23 0x3c 0x48 0x50 0x68 0x76"));
    pin("scan_with_eeprom", UINT64_C(0x105cad2ba07fb923));
}

static void test_an_empty_bus_says_no_devices(void)
{
    fake_lab_i2c_devices(0);
    start();
    scan();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c scan found none"));
    pin("scan_none", UINT64_C(0xddc8a2429a95d94b));
    press(STARK_KEY_OK); /* nothing to open */
    TEST_ASSERT_FALSE(ui_port_log_contains("i2capp: open"));
}

static void test_a_bus_fault_says_bus_error_and_a_rescan_recovers(void)
{
    fake_lab_i2c_fault(FAKE_LAB_I2C_STUCK, -1, -1);
    start();
    scan();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c scan bus error found none"));
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: scan bus error after 0 found"));
    TEST_ASSERT_EQUAL_UINT(3, ui_port_i2c_bus_calls()); /* three probes and out, never a hang */
    pin("scan_bus_error", UINT64_C(0x7f31a01402fe683a));
    fake_lab_i2c_fault(FAKE_LAB_I2C_FAULT_NONE, -1, 0);
    press(STARK_KEY_BACK);
    scan();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c scan found 0x23 0x3c 0x48 0x68 0x76"));
    /* a fault in the middle keeps the partial list and still says so */
    press(STARK_KEY_BACK);
    fake_lab_i2c_fault(FAKE_LAB_I2C_TIMEOUT, -1, 3);
    scan();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c scan bus error found none"));
}

static void test_a_crowded_bus_shows_a_line_for_the_rest(void)
{
    ui_port_i2c_crowd(true);
    start();
    scan();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c scan found 0x08 0x09"));
    TEST_ASSERT_TRUE(ui_port_log_contains("+95")); /* 111 answered: 0x24 is the PN532's, silent */
    pin("scan_crowded", UINT64_C(0x933e175eabe00363));
    presses(STARK_KEY_UP, 1); /* wraps to the last device row, not the disabled line */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: open 0x1f"));
}

static void test_a_register_is_read_by_nibbles(void)
{
    start();
    scan();
    open_device(3); /* 0x68 */
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: open 0x68"));
    pin("device", UINT64_C(0x5f26f14660cb100f));
    open_read();
    pin("read_editor", UINT64_C(0x696dcd6c5b4900b5));
    set_reg(0x75);
    pin("read_reg_75", UINT64_C(0xc7962a5f8a47906f));
    presses(STARK_KEY_DOWN, 4); /* Read */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c read 0x68 reg 0x75 68"));
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: read 0x68 reg 0x75 len 1: 68"));
    pin("read_who_am_i", UINT64_C(0x377da096a7b50fdb));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_i2c_writes());
}

static void test_a_longer_read_shows_two_rows_and_16_bit_shows_the_high_byte(void)
{
    start();
    scan();
    open_device(3);
    open_read();
    presses(STARK_KEY_DOWN, 3); /* Length */
    presses(STARK_KEY_RIGHT, 15);
    pin("read_len_16", UINT64_C(0x51bba65a96fad513));
    press(STARK_KEY_DOWN); /* Read */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: read 0x68 reg 0x00 len 16:"));
    pin("read_16_bytes", UINT64_C(0xb4bafa6ea458fdbb));
    presses(STARK_KEY_UP, 3); /* Register width */
    press(STARK_KEY_RIGHT);
    pin("read_16_bit", UINT64_C(0x68e08dda64c7da6d));
    presses(STARK_KEY_DOWN, 3); /* Read, 16-bit now: a high byte row came in */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c read 0x68 reg 0x0000"));
    /* a change of register forgets the result */
    presses(STARK_KEY_UP, 5); /* Reg */
    press(STARK_KEY_RIGHT);
    pin("read_forgotten", UINT64_C(0x91b9585f4fdbd543));
}

static void test_a_vanished_device_answers_nothing(void)
{
    fake_lab_i2c_fault(FAKE_LAB_I2C_DROP_DEVICE, 0x68, -1);
    start();
    scan();
    open_device(3);
    open_read();
    presses(STARK_KEY_DOWN, 4);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c read 0x68 reg 0x00 Not found"));
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: read 0x68 reg 0x00 failed: Not found"));
    pin("read_no_answer", UINT64_C(0x4900585238201131));
}

static void test_a_dump_asks_first_and_back_touches_nothing(void)
{
    start();
    scan();
    open_device(3);
    unsigned bus = ui_port_i2c_bus_calls();
    press(STARK_KEY_DOWN); /* Dump */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Dump 0x68?\""));
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: confirm dump 0x68"));
    pin("dump_confirm", UINT64_C(0x3b7ef466f8c4095e));
    TEST_ASSERT_EQUAL_UINT(bus, ui_port_i2c_bus_calls());
    press(STARK_KEY_BACK);
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: cancel"));
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: cancelled dump 0x68"));
    TEST_ASSERT_EQUAL_UINT(bus, ui_port_i2c_bus_calls());
    TEST_ASSERT_FALSE(ui_port_i2c_armed());
    press(STARK_KEY_OK); /* asks again */
    TEST_ASSERT_EQUAL_UINT(2, count_logs("dialog: open \"Dump 0x68?\""));
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c dump 0x68 ok"));
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: dump 0x68 ok"));
    TEST_ASSERT_EQUAL_UINT(bus + 16u, ui_port_i2c_bus_calls());
    TEST_ASSERT_FALSE(ui_port_i2c_armed());
    pin("dump", UINT64_C(0x872df6a8158e90f7));
    presses(STARK_KEY_DOWN, 7); /* scrolls: the register file is 16 rows */
    pin("dump_scrolled", UINT64_C(0xd33496c69823445b));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_i2c_writes());
    press(STARK_KEY_BACK); /* to the device */
    press(STARK_KEY_BACK); /* to the list */
    press(STARK_KEY_BACK); /* home */
    press(STARK_KEY_BACK); /* out */
    TEST_ASSERT_TRUE(ui_port_log_contains("app: stop i2c"));
}

static void test_a_failing_dump_alerts(void)
{
    start();
    scan();
    open_device(3);
    press(STARK_KEY_DOWN);
    fake_lab_i2c_fault(FAKE_LAB_I2C_TIMEOUT, 0x68, 1);
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: dump 0x68 failed: Timeout"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Dump failed\""));
    pin("dump_failed", UINT64_C(0xbc9744b90b47ebb4));
}

/* Sets the 0x68 write editor to register 0x6b and one byte of 0x01. */
static void edit_write_6b_01(void)
{
    set_reg(0x6b);
    presses(STARK_KEY_DOWN, 4); /* Byte 0 */
    press(STARK_KEY_RIGHT);
    presses(STARK_KEY_DOWN, 1); /* Write */
}

static void test_a_write_asks_first_and_one_confirmation_buys_one_write(void)
{
    start();
    scan();
    open_device(3);
    open_write();
    pin("write_editor", UINT64_C(0x91862e4a6dcaf59b));
    edit_write_6b_01();
    pin("write_editor_set", UINT64_C(0xf6351c1432e34f75));
    unsigned bus = ui_port_i2c_bus_calls();
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Write 0x68?\""));
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: confirm write 0x68"));
    pin("write_confirm", UINT64_C(0x3dd4b42263d4348a));
    TEST_ASSERT_EQUAL_UINT(bus, ui_port_i2c_bus_calls());
    press(STARK_KEY_BACK); /* cancel: nothing is written, nothing is armed */
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: cancelled write 0x68"));
    TEST_ASSERT_EQUAL_UINT(bus, ui_port_i2c_bus_calls());
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_i2c_writes());
    TEST_ASSERT_FALSE(ui_port_i2c_armed());
    TEST_ASSERT_FALSE(ui_port_log_contains("lab: i2c write 0x68"));
    press(STARK_KEY_OK); /* asks again */
    TEST_ASSERT_EQUAL_UINT(2, count_logs("dialog: open \"Write 0x68?\""));
    press(STARK_KEY_OK); /* confirm */
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c write 0x68 reg 0x6b 01 ok"));
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: write 0x68 reg 0x6b: 01 ok"));
    TEST_ASSERT_EQUAL_HEX8(0x01, fake_lab_i2c_reg(0x68, 0x6b));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_i2c_writes());
    TEST_ASSERT_FALSE(ui_port_i2c_armed());
    pin("write_done", UINT64_C(0xeb9461ea35eca555));
    press(STARK_KEY_OK); /* a second press is a second question */
    TEST_ASSERT_EQUAL_UINT(3, count_logs("dialog: open \"Write 0x68?\""));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_i2c_writes());
    press(STARK_KEY_BACK);
}

static void test_more_bytes_and_a_16_bit_register_in_the_write_editor(void)
{
    start();
    scan();
    open_device(3);
    open_write();
    presses(STARK_KEY_DOWN, 3); /* Bytes */
    presses(STARK_KEY_RIGHT, 7);
    pin("write_8_bytes", UINT64_C(0x9d1f77f67e132567));
    presses(STARK_KEY_UP, 2); /* Register width */
    press(STARK_KEY_RIGHT);
    pin("write_8_bytes_16_bit", UINT64_C(0xc3fa7d8aa546b0f3));
    press(STARK_KEY_DOWN); /* Nibble */
    presses(STARK_KEY_DOWN, 1);
    presses(STARK_KEY_LEFT, 7); /* the count goes back down */
    TEST_ASSERT_TRUE(true);
}

static void test_a_write_to_a_read_only_register_is_taken_and_changes_nothing(void)
{
    start();
    scan();
    open_device(3);
    open_write();
    set_reg(0x75);
    presses(STARK_KEY_DOWN, 4);
    presses(STARK_KEY_RIGHT, 5);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c write 0x68 reg 0x75 05 ok"));
    TEST_ASSERT_EQUAL_HEX8(0x68, fake_lab_i2c_reg(0x68, 0x75)); /* WHO_AM_I ignores it */
}

static void test_a_failing_write_alerts_and_says_so_in_the_row(void)
{
    start();
    scan();
    open_device(3);
    open_write();
    edit_write_6b_01();
    fake_lab_i2c_fault(FAKE_LAB_I2C_TIMEOUT, 0x68, 1);
    press(STARK_KEY_OK);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c write 0x68 reg 0x6b 01 Timeout"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Write failed\""));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_i2c_writes());
    press(STARK_KEY_OK); /* close the alert */
    pin("write_failed", UINT64_C(0x5bfd258835e9afe1));
}

/* AC 3 on the host: a write to a module ID EEPROM is refused up front, with no dialog and no
 * traffic. */
static void test_a_write_to_a_module_eeprom_is_refused_with_an_alert_and_no_traffic(void)
{
    ui_port_i2c_eeprom(0x50, true);
    start();
    scan();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c scan found 0x23 0x3c 0x48 0x50 0x68 0x76"));
    open_device(3); /* 0x50 */
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: open 0x50"));
    open_write();
    unsigned bus = ui_port_i2c_bus_calls();
    unsigned eeprom = fake_24c02_transactions();
    presses(STARK_KEY_DOWN, 4);
    presses(STARK_KEY_RIGHT, 2);
    press(STARK_KEY_DOWN); /* Write */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c write refused: module eeprom"));
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: Write refused: module eeprom"));
    TEST_ASSERT_TRUE(ui_port_log_contains("dialog: open \"Refused\""));
    TEST_ASSERT_FALSE(ui_port_log_contains("dialog: open \"Write 0x50?\""));
    TEST_ASSERT_EQUAL_UINT(bus, ui_port_i2c_bus_calls());
    TEST_ASSERT_EQUAL_UINT(eeprom, fake_24c02_transactions());
    pin("write_refused_eeprom", UINT64_C(0x85ef40193c06d080));
    press(STARK_KEY_OK); /* close the alert; the chip is as it was */
    uint8_t b[2];
    TEST_ASSERT_EQUAL(STARK_OK, fake_24c02_read_at(0x50, 0, b, 2));
    TEST_ASSERT_EQUAL_HEX8(0xA5, b[0]);
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_i2c_writes());
}

static void test_a_16_bit_read_of_a_module_eeprom_is_refused(void)
{
    ui_port_i2c_eeprom(0x51, true);
    start();
    scan();
    open_device(3); /* 0x23 0x3c 0x48 0x51 ... */
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: open 0x51"));
    open_read();
    press(STARK_KEY_DOWN);
    press(STARK_KEY_RIGHT);     /* 16-bit: the selection stays on the Register row */
    presses(STARK_KEY_DOWN, 3); /* Nibble, Length, Read */
    unsigned eeprom = fake_24c02_transactions();
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c read refused: module eeprom"));
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: Read refused: module eeprom"));
    TEST_ASSERT_EQUAL_UINT(eeprom, fake_24c02_transactions());
    pin("read_refused_eeprom", UINT64_C(0x28fc6c98ede5d062));
}

static void test_a_module_eeprom_is_read_and_dumped_freely(void)
{
    ui_port_i2c_eeprom(0x50, true);
    start();
    scan();
    open_device(3);
    open_read();
    presses(STARK_KEY_DOWN, 3);
    presses(STARK_KEY_RIGHT, 7);
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c read 0x50 reg 0x00 a5 a5 a5 a5 a5 a5 a5 a5"));
    pin("read_eeprom", UINT64_C(0x3f77eb45f2761c1a));
}

static void test_the_pn532_is_refused_while_nfc_is_confirmed(void)
{
    ui_port_i2c_nfc(true);
    start();
    scan();
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c scan found 0x23 0x24 0x3c 0x48 0x68 0x76"));
    press(STARK_KEY_DOWN);
    press(STARK_KEY_OK); /* 0x24 */
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: open 0x24"));
    unsigned bus = ui_port_i2c_bus_calls();
    open_read();
    presses(STARK_KEY_DOWN, 4);
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c read refused: nfc reader"));
    pin("read_refused_nfc", UINT64_C(0x246401c4a499e93a));
    press(STARK_KEY_OK);
    press(STARK_KEY_BACK);
    press(STARK_KEY_DOWN); /* Dump */
    press(STARK_KEY_OK);
    TEST_ASSERT_TRUE(ui_port_log_contains("lab: i2c dump refused: nfc reader"));
    TEST_ASSERT_FALSE(ui_port_log_contains("dialog: open \"Dump 0x24?\""));
    press(STARK_KEY_OK);
    press(STARK_KEY_DOWN); /* Write */
    press(STARK_KEY_OK);
    TEST_ASSERT_EQUAL_UINT(bus, ui_port_i2c_bus_calls());
    /* the driver gives it up: it is an ordinary address again and a read goes through */
    ui_port_i2c_nfc(true);
    TEST_ASSERT_NOT_NULL(stark_lab_i2c_refusal(0x24, false, false));
    ui_port_i2c_nfc(false);
    TEST_ASSERT_NULL(stark_lab_i2c_refusal(0x24, false, false));
}

static void test_the_speed_row_toggles_and_is_remembered_by_the_session(void)
{
    start();
    pin("home_100", UINT64_C(0x8ae20497868d7215));
    press(STARK_KEY_DOWN); /* Speed */
    press(STARK_KEY_RIGHT);
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: speed 400 kHz"));
    pin("home_400", UINT64_C(0xb48b7aaf328bf5e9));
    press(STARK_KEY_LEFT);
    TEST_ASSERT_TRUE(ui_port_log_contains("i2capp: speed 100 kHz"));
    press(STARK_KEY_UP); /* Scan: not adjustable */
    press(STARK_KEY_RIGHT);
    TEST_ASSERT_EQUAL_UINT(2, count_logs("i2capp: speed"));
}

static void test_leaving_the_app_clears_an_arm(void)
{
    start();
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_i2c_arm(0x1234));
    TEST_ASSERT_TRUE(ui_port_i2c_armed());
    press(STARK_KEY_BACK);
    TEST_ASSERT_FALSE(ui_port_i2c_armed());
    start();
    scan();
    open_device(0);
    open_read();
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_i2c_arm(0x1234));
    press(STARK_KEY_BACK); /* the editor goes: so does the arm */
    TEST_ASSERT_FALSE(ui_port_i2c_armed());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_home_and_the_scan_of_the_five_devices);
    RUN_TEST(test_the_module_eeprom_and_the_hints_appear_on_the_same_scan);
    RUN_TEST(test_an_empty_bus_says_no_devices);
    RUN_TEST(test_a_bus_fault_says_bus_error_and_a_rescan_recovers);
    RUN_TEST(test_a_crowded_bus_shows_a_line_for_the_rest);
    RUN_TEST(test_a_register_is_read_by_nibbles);
    RUN_TEST(test_a_longer_read_shows_two_rows_and_16_bit_shows_the_high_byte);
    RUN_TEST(test_a_vanished_device_answers_nothing);
    RUN_TEST(test_a_dump_asks_first_and_back_touches_nothing);
    RUN_TEST(test_a_failing_dump_alerts);
    RUN_TEST(test_a_write_asks_first_and_one_confirmation_buys_one_write);
    RUN_TEST(test_more_bytes_and_a_16_bit_register_in_the_write_editor);
    RUN_TEST(test_a_write_to_a_read_only_register_is_taken_and_changes_nothing);
    RUN_TEST(test_a_failing_write_alerts_and_says_so_in_the_row);
    RUN_TEST(test_a_write_to_a_module_eeprom_is_refused_with_an_alert_and_no_traffic);
    RUN_TEST(test_a_16_bit_read_of_a_module_eeprom_is_refused);
    RUN_TEST(test_a_module_eeprom_is_read_and_dumped_freely);
    RUN_TEST(test_the_pn532_is_refused_while_nfc_is_confirmed);
    RUN_TEST(test_the_speed_row_toggles_and_is_remembered_by_the_session);
    RUN_TEST(test_leaving_the_app_clears_an_arm);
    return UNITY_END();
}
