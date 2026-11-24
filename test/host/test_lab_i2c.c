/*
 * test_lab_i2c.c — the I2C scanner's rules (components/stark_lab/lab_i2c.c, lab_i2c_hints.c,
 * TASKS.md STARK-0704, ADR-0023 §5) against the independent vectors of scripts/gen_lab_vectors.py
 * and over the test-only bus (test/fake/fake_lab.c) behind the transport's own guard (the host shim
 * of lab_transport_i2c.c): the sweep (none, one, all, the edges, timeouts, three errors in a row),
 * the address range, the refusals of a module ID EEPROM and of the PN532 while NFC is confirmed
 * (from the core and again from the transport, with no traffic on the bus), the arm tied to a
 * fingerprint (consumed, wrong payload, stale, replaced), read, dump and write lengths at each
 * edge, hex text, the hints table, and what each fake device answers. Proves rules and protocol
 * over a model, never a device on a bus (ROADMAP V1 exit 13).
 */
#include <stdio.h>
#include <string.h>
#include "fake_24c02.h"
#include "fake_lab.h"
#include "fake_lab_i2c_vectors.h"
#include "lab_i2c.h"
#include "stark_board.h"
#include "unity.h"

/* ---- the world the rules read ------------------------------------------------------------- */

static stark_board_pins_t s_map;
static uint32_t s_caps;
static uint32_t s_now_ms;

const stark_board_pins_t *stark_board_pins(void)
{
    return &s_map;
}

uint32_t stark_module_caps(void)
{
    return s_caps;
}

static stark_err_t ext_probe(uint8_t addr)
{
    return fake_24c02_probe(addr);
}

static stark_err_t ext_xfer(uint8_t addr, const uint8_t *w, size_t wn, uint8_t *r, size_t rn)
{
    if (wn == 1u && rn > 0u) {
        return fake_24c02_read_at(addr, w[0], r, rn);
    }
    return wn > 0u && rn == 0u ? fake_24c02_write(addr, w, wn) : STARK_ERR_INVALID_ARG;
}

/* The bus the rules run over: the transport's guard in front of the fake bus. */
static stark_err_t io_probe(void *ctx, uint8_t addr, uint32_t timeout_ms)
{
    (void)ctx;
    return stark_lab_transport_i2c_probe(addr, timeout_ms);
}

static stark_err_t io_xfer(void *ctx, uint8_t addr, uint32_t hz, const uint8_t *w, size_t wn,
                           uint8_t *r, size_t rn, uint32_t timeout_ms)
{
    (void)ctx;
    return stark_lab_transport_i2c_xfer(addr, hz, w, wn, r, rn, timeout_ms);
}

static bool io_nfc(void *ctx)
{
    (void)ctx;
    return (s_caps & STARK_CAP_NFC) != 0u;
}

static uint32_t io_now(void *ctx)
{
    (void)ctx;
    return s_now_ms;
}

static lab_i2c_t s_c;

void setUp(void)
{
    s_map = (stark_board_pins_t){
        .lab_uart_tx = 38,
        .lab_uart_rx = 39,
        .module = {{.cs = 40, .irq0 = 41, .irq1 = 42, .en = -1, .eeprom_addr = 0x50},
                   {.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1, .eeprom_addr = 0x51}},
    };
    s_caps = 0;
    s_now_ms = 1000;
    fake_lab_reset();
    fake_24c02_reset();
    fake_lab_i2c_ext(ext_probe, ext_xfer);
    const lab_i2c_io_t io = {
        .probe = io_probe, .xfer = io_xfer, .nfc_confirmed = io_nfc, .now_ms = io_now};
    lab_i2c_init(&s_c, &io, &s_map);
}

void tearDown(void)
{}

static void put_eeprom(uint8_t addr)
{
    uint8_t image[256];
    memset(image, 0xA5, sizeof image);
    fake_24c02_put(addr, image, sizeof image);
}

/* ---- the sweep over the vectors ------------------------------------------------------------ */

static const uint8_t *g_acks;
static size_t g_n_acks;
static const uint8_t *g_errs;
static size_t g_n_errs;
static unsigned g_probes;

static bool in(const uint8_t *v, size_t n, uint8_t a)
{
    for (size_t i = 0; i < n; i++) {
        if (v[i] == a) {
            return true;
        }
    }
    return false;
}

static stark_err_t vec_probe(void *ctx, uint8_t addr, uint32_t timeout_ms)
{
    (void)ctx;
    TEST_ASSERT_EQUAL_UINT32(LAB_I2C_PROBE_MS, timeout_ms);
    g_probes++;
    if (in(g_errs, g_n_errs, addr)) {
        return STARK_ERR_TIMEOUT;
    }
    return in(g_acks, g_n_acks, addr) ? STARK_OK : STARK_ERR_NOT_FOUND;
}

static void test_every_sweep_vector_matches_the_independent_reference(void)
{
    TEST_ASSERT_TRUE(FAKE_LAB_SWEEP_VECS >= 15u);
    lab_i2c_io_t io = {.probe = vec_probe, .nfc_confirmed = io_nfc, .now_ms = io_now};
    lab_i2c_init(&s_c, &io, &s_map);
    for (size_t i = 0; i < FAKE_LAB_SWEEP_VECS; i++) {
        const fake_lab_sweep_vec_t *v = &fake_lab_sweep_vecs[i];
        g_acks = v->acks;
        g_n_acks = v->n_acks;
        g_errs = v->errs;
        g_n_errs = v->n_errs;
        g_probes = 0;
        stark_lab_scan_t out;
        stark_err_t err = lab_i2c_scan(&s_c, &out);
        TEST_ASSERT_EQUAL_MESSAGE(v->status == 0u ? STARK_OK : STARK_ERR_IO, err, v->name);
        TEST_ASSERT_EQUAL_MESSAGE(err, out.status, v->name);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(v->n_found, out.n, v->name);
        if (v->n_found > 0u) {
            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(v->found, out.addr, v->n_found, v->name);
        }
        TEST_ASSERT_EQUAL_UINT_MESSAGE(v->errors, out.errors, v->name);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(v->probes, g_probes, v->name);
        TEST_ASSERT_TRUE_MESSAGE(g_probes <= STARK_LAB_I2C_ADDRS, v->name);
    }
}

static void test_the_sweep_is_bounded_whatever_the_bus_does(void)
{
    /* a NACK storm: every address NACKs, 112 probes, an empty list, no error */
    fake_lab_i2c_fault(FAKE_LAB_I2C_NACK_ALL, -1, -1);
    stark_lab_scan_t out;
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_scan(&s_c, &out));
    TEST_ASSERT_EQUAL_UINT(0, out.n);
    TEST_ASSERT_EQUAL_UINT(112, fake_lab_i2c_calls());
    /* a stuck bus: three timeouts and out */
    fake_lab_reset();
    fake_lab_i2c_ext(ext_probe, ext_xfer);
    fake_lab_i2c_fault(FAKE_LAB_I2C_STUCK, -1, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_i2c_scan(&s_c, &out));
    TEST_ASSERT_EQUAL_UINT(0, out.n);
    TEST_ASSERT_EQUAL_UINT(3, out.errors);
    TEST_ASSERT_EQUAL_UINT(3, fake_lab_i2c_calls());
}

/* ---- the sweep over the fake bus ------------------------------------------------------------ */

static void test_the_scan_finds_exactly_the_five_devices(void)
{
    stark_lab_scan_t out;
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_scan(&s_c, &out));
    const uint8_t want[] = {0x23, 0x3c, 0x48, 0x68, 0x76};
    TEST_ASSERT_EQUAL_UINT(5, out.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out.addr, 5);
    TEST_ASSERT_EQUAL_UINT(0, out.errors);
    TEST_ASSERT_EQUAL_UINT(112, fake_lab_i2c_calls());
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_i2c_writes());
}

static void test_the_scan_follows_the_device_mask(void)
{
    stark_lab_scan_t out;
    fake_lab_i2c_devices(0);
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_scan(&s_c, &out));
    TEST_ASSERT_EQUAL_UINT(0, out.n);
    fake_lab_i2c_devices(0x05); /* 0x23 and 0x48 */
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_scan(&s_c, &out));
    TEST_ASSERT_EQUAL_UINT(2, out.n);
    TEST_ASSERT_EQUAL_UINT8(0x23, out.addr[0]);
    TEST_ASSERT_EQUAL_UINT8(0x48, out.addr[1]);
    fake_lab_i2c_devices(0xFF); /* bits beyond the five are ignored */
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_scan(&s_c, &out));
    TEST_ASSERT_EQUAL_UINT(5, out.n);
}

static void test_the_module_eeproms_and_the_pn532_join_the_one_bus(void)
{
    put_eeprom(0x50);
    stark_lab_scan_t out;
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_scan(&s_c, &out));
    const uint8_t want[] = {0x23, 0x3c, 0x48, 0x50, 0x68, 0x76};
    TEST_ASSERT_EQUAL_UINT(6, out.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out.addr, 6);
    fake_24c02_remove(0x50);
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_scan(&s_c, &out));
    TEST_ASSERT_EQUAL_UINT(5, out.n);
}

static void test_a_timeout_at_one_address_loses_only_it(void)
{
    fake_lab_i2c_fault(FAKE_LAB_I2C_TIMEOUT, 0x48, -1);
    stark_lab_scan_t out;
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_scan(&s_c, &out));
    TEST_ASSERT_EQUAL_UINT(4, out.n);
    TEST_ASSERT_EQUAL_UINT(1, out.errors);
    /* a counted timeout fault clears itself */
    fake_lab_i2c_fault(FAKE_LAB_I2C_TIMEOUT, -1, 2);
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_scan(&s_c, &out)); /* two in a row, then a NACK */
    TEST_ASSERT_EQUAL_UINT(2, out.errors);
    TEST_ASSERT_EQUAL_UINT(5, out.n);
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_scan(&s_c, &out));
    TEST_ASSERT_EQUAL_UINT(0, out.errors);
    fake_lab_i2c_fault(FAKE_LAB_I2C_FAULT_NONE, -1, 0);
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_scan(&s_c, &out));
    TEST_ASSERT_EQUAL_UINT(5, out.n);
}

static void test_a_device_that_vanishes_is_found_and_then_gone(void)
{
    fake_lab_i2c_fault(FAKE_LAB_I2C_DROP_DEVICE, 0x68, -1);
    stark_lab_scan_t out;
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_scan(&s_c, &out));
    TEST_ASSERT_EQUAL_UINT(5, out.n); /* it answered once */
    uint8_t b[1];
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND,
                      lab_i2c_read(&s_c, 0x68, 0x75, false, b, 1, STARK_LAB_I2C_HZ_STD));
    /* and a drop at another address leaves this one alone */
    fake_lab_i2c_fault(FAKE_LAB_I2C_DROP_DEVICE, 0x23, -1);
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_read(&s_c, 0x68, 0x75, false, b, 1, STARK_LAB_I2C_HZ_STD));
}

/* ---- reads, and what each fake device answers ----------------------------------------------- */

static uint8_t rd1(uint8_t addr, uint16_t reg)
{
    uint8_t b = 0xEE;
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_read(&s_c, addr, reg, false, &b, 1, STARK_LAB_I2C_HZ_STD));
    return b;
}

static void test_who_am_i_and_the_other_fake_devices(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x68, rd1(0x68, 0x75));
    TEST_ASSERT_EQUAL_HEX8(0x40, rd1(0x68, 0x6b));
    TEST_ASSERT_EQUAL_HEX8(0x60, rd1(0x76, 0xd0));
    uint8_t b[4];
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_read(&s_c, 0x23, 0, false, b, 4, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_HEX8(0x01, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0x23, b[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, b[2]);
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_read(&s_c, 0x3c, 0, false, b, 4, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_HEX8(0x00, b[1]);
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_read(&s_c, 0x48, 0, false, b, 4, STARK_LAB_I2C_HZ_STD));
    const uint8_t temp[] = {0x19, 0x00, 0x19, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(temp, b, 4);
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_read(&s_c, 0x48, 1, false, b, 2, STARK_LAB_I2C_HZ_FAST));
    TEST_ASSERT_EQUAL_HEX8(0x60, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0xa0, b[1]);
}

static void test_a_read_of_an_address_nobody_has_is_a_nack(void)
{
    uint8_t b[1];
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND,
                      lab_i2c_read(&s_c, 0x10, 0, false, b, 1, STARK_LAB_I2C_HZ_STD));
    fake_lab_i2c_fault(FAKE_LAB_I2C_STUCK, -1, -1);
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT,
                      lab_i2c_read(&s_c, 0x68, 0x75, false, b, 1, STARK_LAB_I2C_HZ_STD));
}

static void test_a_module_eeprom_reads_what_is_in_it(void)
{
    put_eeprom(0x50);
    uint8_t b[8];
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_read(&s_c, 0x50, 0, false, b, 8, STARK_LAB_I2C_HZ_STD));
    for (size_t i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xA5, b[i]);
    }
    /* no EEPROM at 0x51: a NACK */
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND,
                      lab_i2c_read(&s_c, 0x51, 0, false, b, 8, STARK_LAB_I2C_HZ_STD));
}

/* ---- argument edges ------------------------------------------------------------------------- */

static void test_addresses_outside_0x08_to_0x77_are_invalid(void)
{
    uint8_t b[16] = {0};
    const uint8_t v[1] = {1};
    uint8_t bad[] = {0x00, 0x07, 0x78, 0x7f, 0x80, 0xff};
    for (size_t i = 0; i < sizeof bad; i++) {
        TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                          lab_i2c_read(&s_c, bad[i], 0, false, b, 1, STARK_LAB_I2C_HZ_STD));
        TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                          lab_i2c_dump(&s_c, bad[i], b, STARK_LAB_I2C_HZ_STD));
        TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                          lab_i2c_write(&s_c, bad[i], 0, false, v, 1, STARK_LAB_I2C_HZ_STD));
        TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_i2c_probe(bad[i], 5));
        TEST_ASSERT_EQUAL(
            STARK_ERR_INVALID_ARG,
            stark_lab_transport_i2c_xfer(bad[i], STARK_LAB_I2C_HZ_STD, v, 1, NULL, 0, 5));
    }
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_i2c_calls());
    /* the edges themselves are fine */
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND,
                      lab_i2c_read(&s_c, 0x08, 0, false, b, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND,
                      lab_i2c_read(&s_c, 0x77, 0, false, b, 1, STARK_LAB_I2C_HZ_STD));
}

static void test_read_lengths_registers_and_speeds(void)
{
    uint8_t b[32];
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      lab_i2c_read(&s_c, 0x68, 0, false, b, 0, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_read(&s_c, 0x68, 0, false, b, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_read(&s_c, 0x68, 0, false, b, 16, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      lab_i2c_read(&s_c, 0x68, 0, false, b, 17, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      lab_i2c_read(&s_c, 0x68, 0, false, NULL, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      lab_i2c_read(&s_c, 0x68, 0x100, false, b, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL(STARK_OK,
                      lab_i2c_read(&s_c, 0x68, 0x00ff, false, b, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_i2c_read(&s_c, 0x68, 0, false, b, 1, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_i2c_read(&s_c, 0x68, 0, false, b, 1, 1000000));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_i2c_read(&s_c, 0x68, 0, false, b, 1, 50000));
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_read(&s_c, 0x68, 0, false, b, 1, STARK_LAB_I2C_HZ_FAST));
}

static void test_a_16_bit_register_goes_out_high_byte_first(void)
{
    /* the model's 0x68 takes the first byte as its pointer and stores the rest from it: a 16-bit
     * register on an 8-bit device is a write of the low byte into the high one's register, which
     * is exactly what the hardware does and why the app says a read can change a device */
    uint8_t b[2];
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_read(&s_c, 0x68, 0x1255, true, b, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_HEX8(0x55, fake_lab_i2c_reg(0x68, 0x12));
}

/* ---- refusals ------------------------------------------------------------------------------- */

static void test_every_refusal_vector_matches_the_independent_table(void)
{
    TEST_ASSERT_TRUE(FAKE_LAB_REFUSAL_VECS >= 100u);
    for (size_t i = 0; i < FAKE_LAB_REFUSAL_VECS; i++) {
        const fake_lab_refusal_vec_t *v = &fake_lab_refusal_vecs[i];
        s_caps = v->nfc ? STARK_CAP_NFC : 0u;
        char msg[64];
        snprintf(msg, sizeof msg, "addr 0x%02x wn %u rn %u nfc %u", v->addr, v->wn, v->rn, v->nfc);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(v->expect,
                                       (unsigned)lab_i2c_refusal(&s_c, v->addr, v->wn, v->rn), msg);
        /* the transport's guard agrees, with the same inputs */
        bool allowed = stark_lab_i2c_allowed(&s_map, v->nfc != 0u, v->addr, v->wn, v->rn);
        TEST_ASSERT_EQUAL_MESSAGE(v->expect == 0u, allowed, msg);
    }
}

static void test_refusal_texts(void)
{
    TEST_ASSERT_EQUAL_STRING("module eeprom", lab_i2c_refusal_text(LAB_I2C_REFUSE_EEPROM));
    TEST_ASSERT_EQUAL_STRING("nfc reader", lab_i2c_refusal_text(LAB_I2C_REFUSE_NFC));
    TEST_ASSERT_EQUAL_STRING("", lab_i2c_refusal_text(LAB_I2C_REFUSE_NONE));
    TEST_ASSERT_EQUAL_STRING("", lab_i2c_refusal_text((lab_i2c_refusal_t)99));
}

static void test_a_write_to_a_module_eeprom_is_refused_with_no_traffic(void)
{
    put_eeprom(0x50);
    put_eeprom(0x51);
    const uint8_t v[2] = {0xDE, 0xAD};
    for (uint8_t addr = 0x50; addr <= 0x51; addr++) {
        /* armed for exactly this write: the refusal does not need the arm and the arm is spent */
        lab_i2c_arm(&s_c, lab_i2c_fp_write(addr, 0x00, false, v, 2));
        unsigned bus = fake_lab_i2c_calls();
        unsigned eeprom = fake_24c02_transactions();
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                          lab_i2c_write(&s_c, addr, 0x00, false, v, 2, STARK_LAB_I2C_HZ_STD));
        TEST_ASSERT_FALSE(lab_i2c_armed(&s_c));
        TEST_ASSERT_EQUAL_UINT(bus, fake_lab_i2c_calls());
        TEST_ASSERT_EQUAL_UINT(eeprom, fake_24c02_transactions());
        /* and without any arm it is the same refusal, not "not armed" */
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                          lab_i2c_write(&s_c, addr, 0x00, false, v, 2, STARK_LAB_I2C_HZ_STD));
        uint8_t b[2];
        TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_read(&s_c, addr, 0, false, b, 2, STARK_LAB_I2C_HZ_STD));
        TEST_ASSERT_EQUAL_HEX8(0xA5, b[0]);
    }
}

static void test_the_transport_refuses_what_the_core_would_if_it_were_bypassed(void)
{
    put_eeprom(0x50);
    const uint8_t w[3] = {0x00, 0x11, 0x22};
    unsigned bus = fake_lab_i2c_calls();
    uint8_t r[2];
    /* a write, a write of more than a pointer before a read: both could change the chip */
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      stark_lab_transport_i2c_xfer(0x50, STARK_LAB_I2C_HZ_STD, w, 3, NULL, 0, 5));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      stark_lab_transport_i2c_xfer(0x50, STARK_LAB_I2C_HZ_STD, w, 2, r, 2, 5));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      stark_lab_transport_i2c_xfer(0x50, STARK_LAB_I2C_HZ_STD, w, 1, NULL, 0, 5));
    TEST_ASSERT_EQUAL_UINT(bus, fake_lab_i2c_calls());
    /* the pointer-and-read the module task itself uses is allowed */
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_lab_transport_i2c_xfer(0x50, STARK_LAB_I2C_HZ_STD, w, 1, r, 2, 5));
    TEST_ASSERT_EQUAL(bus + 1u, fake_lab_i2c_calls());
    /* the PN532, while NFC is confirmed: not even a read */
    s_caps = STARK_CAP_NFC;
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      stark_lab_transport_i2c_xfer(0x24, STARK_LAB_I2C_HZ_STD, w, 1, r, 2, 5));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      stark_lab_transport_i2c_xfer(0x24, STARK_LAB_I2C_HZ_STD, w, 3, NULL, 0, 5));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      stark_lab_transport_i2c_xfer(0x24, STARK_LAB_I2C_HZ_STD, NULL, 0, r, 2, 5));
    TEST_ASSERT_EQUAL(bus + 1u, fake_lab_i2c_calls());
    /* the probe is address-only and always allowed: the scan must be able to list them */
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, stark_lab_transport_i2c_probe(0x24, 5));
    /* NFC not confirmed: the driver does not own it any more */
    s_caps = 0;
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND,
                      stark_lab_transport_i2c_xfer(0x24, STARK_LAB_I2C_HZ_STD, w, 1, r, 2, 5));
}

static void test_the_pn532_is_refused_by_the_core_while_nfc_is_confirmed(void)
{
    uint8_t b[16];
    const uint8_t v[1] = {1};
    s_caps = STARK_CAP_NFC;
    unsigned bus = fake_lab_i2c_calls();
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      lab_i2c_read(&s_c, 0x24, 0, false, b, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_i2c_dump(&s_c, 0x24, b, STARK_LAB_I2C_HZ_STD));
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x24, 0, false, v, 1));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      lab_i2c_write(&s_c, 0x24, 0, false, v, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_FALSE(lab_i2c_armed(&s_c));
    TEST_ASSERT_EQUAL_UINT(bus, fake_lab_i2c_calls());
    /* not confirmed: it is just another address (nobody answers it in this model) */
    s_caps = 0;
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND,
                      lab_i2c_read(&s_c, 0x24, 0, false, b, 1, STARK_LAB_I2C_HZ_STD));
}

static void test_a_16_bit_read_of_a_module_eeprom_is_a_write_and_refused(void)
{
    put_eeprom(0x50);
    uint8_t b[2];
    unsigned eeprom = fake_24c02_transactions();
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      lab_i2c_read(&s_c, 0x50, 0x0000, true, b, 2, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_UINT(eeprom, fake_24c02_transactions());
}

/* ---- the arm -------------------------------------------------------------------------------- */

static void test_a_write_needs_its_arm_and_one_arm_buys_one_write(void)
{
    const uint8_t v[1] = {0x01};
    TEST_ASSERT_EQUAL(STARK_ERR_STATE,
                      lab_i2c_write(&s_c, 0x68, 0x6b, false, v, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_i2c_calls());
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x68, 0x6b, false, v, 1));
    TEST_ASSERT_TRUE(lab_i2c_armed(&s_c));
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_write(&s_c, 0x68, 0x6b, false, v, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_FALSE(lab_i2c_armed(&s_c));
    TEST_ASSERT_EQUAL_HEX8(0x01, rd1(0x68, 0x6b)); /* it reads back */
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_i2c_writes());
    /* the second one needs a second confirmation */
    TEST_ASSERT_EQUAL(STARK_ERR_STATE,
                      lab_i2c_write(&s_c, 0x68, 0x6b, false, v, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_i2c_writes());
}

static void test_a_different_payload_is_not_what_was_confirmed(void)
{
    const uint8_t one[1] = {0x01};
    const uint8_t two[1] = {0x02};
    const uint8_t pair[2] = {0x01, 0x00};
    struct {
        uint8_t addr;
        uint16_t reg;
        bool reg16;
        const uint8_t *v;
        size_t n;
    } other[] = {
        {0x68, 0x6b, false, two, 1}, {0x68, 0x6c, false, one, 1},  {0x69, 0x6b, false, one, 1},
        {0x68, 0x6b, true, one, 1},  {0x68, 0x6b, false, pair, 2},
    };
    for (size_t i = 0; i < sizeof other / sizeof other[0]; i++) {
        lab_i2c_arm(&s_c, lab_i2c_fp_write(0x68, 0x6b, false, one, 1));
        TEST_ASSERT_EQUAL(STARK_ERR_STATE,
                          lab_i2c_write(&s_c, other[i].addr, other[i].reg, other[i].reg16,
                                        other[i].v, other[i].n, STARK_LAB_I2C_HZ_STD));
        TEST_ASSERT_FALSE_MESSAGE(lab_i2c_armed(&s_c), "a miss spends the arm");
    }
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_i2c_calls());
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_i2c_writes());
}

static void test_a_stale_or_replaced_or_cleared_arm_does_not_write(void)
{
    const uint8_t v[1] = {0x01};
    const uint8_t w[1] = {0x02};
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x68, 0x6b, false, v, 1));
    s_now_ms += LAB_I2C_ARM_MS; /* exactly at the limit: still good */
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_write(&s_c, 0x68, 0x6b, false, v, 1, STARK_LAB_I2C_HZ_STD));
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x68, 0x6b, false, v, 1));
    s_now_ms += LAB_I2C_ARM_MS + 1u;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE,
                      lab_i2c_write(&s_c, 0x68, 0x6b, false, v, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_FALSE(lab_i2c_armed(&s_c));
    /* a later arm replaces an earlier one */
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x68, 0x6b, false, v, 1));
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x68, 0x6b, false, w, 1));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE,
                      lab_i2c_write(&s_c, 0x68, 0x6b, false, v, 1, STARK_LAB_I2C_HZ_STD));
    /* leaving the screen clears it */
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x68, 0x6b, false, v, 1));
    lab_i2c_disarm(&s_c);
    TEST_ASSERT_FALSE(lab_i2c_armed(&s_c));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE,
                      lab_i2c_write(&s_c, 0x68, 0x6b, false, v, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_i2c_writes());
    /* the clock may wrap between the arm and the call */
    s_now_ms = 0xFFFFFF00u;
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x68, 0x6b, false, v, 1));
    s_now_ms = 0x00000100u;
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_write(&s_c, 0x68, 0x6b, false, v, 1, STARK_LAB_I2C_HZ_STD));
}

static void test_write_lengths_registers_and_arguments(void)
{
    const uint8_t v[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x68, 0x10, false, v, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      lab_i2c_write(&s_c, 0x68, 0x10, false, v, 0, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_FALSE_MESSAGE(lab_i2c_armed(&s_c), "an invalid call spends the arm too");
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x68, 0x10, false, v, 9));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      lab_i2c_write(&s_c, 0x68, 0x10, false, v, 9, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      lab_i2c_write(&s_c, 0x68, 0x10, false, NULL, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      lab_i2c_write(&s_c, 0x68, 0x110, false, v, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_i2c_write(&s_c, 0x68, 0x10, false, v, 1, 123));
    for (size_t n = 1; n <= 8; n++) {
        lab_i2c_arm(&s_c, lab_i2c_fp_write(0x68, 0x10, false, v, n));
        TEST_ASSERT_EQUAL(STARK_OK,
                          lab_i2c_write(&s_c, 0x68, 0x10, false, v, n, STARK_LAB_I2C_HZ_STD));
        for (size_t i = 0; i < n; i++) {
            TEST_ASSERT_EQUAL_HEX8(v[i], fake_lab_i2c_reg(0x68, (uint8_t)(0x10 + i)));
        }
    }
    /* the 16-bit form goes out with its high byte first: the pointer is the high byte */
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x68, 0x2030, true, v, 1));
    TEST_ASSERT_EQUAL(STARK_OK,
                      lab_i2c_write(&s_c, 0x68, 0x2030, true, v, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_HEX8(0x30, fake_lab_i2c_reg(0x68, 0x20));
    TEST_ASSERT_EQUAL_HEX8(0x01, fake_lab_i2c_reg(0x68, 0x21));
}

static void test_a_write_to_a_read_only_register_does_not_change_it(void)
{
    const uint8_t v[1] = {0x55};
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x68, 0x75, false, v, 1));
    unsigned writes = fake_lab_i2c_writes();
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_write(&s_c, 0x68, 0x75, false, v, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_HEX8(0x68, rd1(0x68, 0x75)); /* WHO_AM_I ignores it */
    TEST_ASSERT_EQUAL_UINT(writes, fake_lab_i2c_writes());
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x76, 0xd0, false, v, 1));
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_write(&s_c, 0x76, 0xd0, false, v, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_HEX8(0x60, rd1(0x76, 0xd0));
    /* the 0x48 pair of bytes: register 0 is read-only, register 1 takes it */
    const uint8_t p[3] = {0x01, 0x12, 0x34};
    uint8_t ro[3] = {0x00, 0x12, 0x34};
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x48, 0x00, false, ro + 1, 2));
    TEST_ASSERT_EQUAL(STARK_OK,
                      lab_i2c_write(&s_c, 0x48, 0x00, false, ro + 1, 2, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_HEX16(0x1900, fake_lab_i2c_word(0));
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x48, 0x01, false, p + 1, 2));
    TEST_ASSERT_EQUAL(STARK_OK,
                      lab_i2c_write(&s_c, 0x48, 0x01, false, p + 1, 2, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_HEX16(0x1234, fake_lab_i2c_word(1));
}

static void test_a_command_write_to_the_display_and_light_sensor_is_taken(void)
{
    const uint8_t cmd[2] = {0x00, 0xAE};
    unsigned writes = fake_lab_i2c_writes();
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x3c, 0x00, false, cmd + 1, 1));
    TEST_ASSERT_EQUAL(STARK_OK,
                      lab_i2c_write(&s_c, 0x3c, 0x00, false, cmd + 1, 1, STARK_LAB_I2C_HZ_STD));
    lab_i2c_arm(&s_c, lab_i2c_fp_write(0x23, 0x10, false, cmd + 1, 1));
    TEST_ASSERT_EQUAL(STARK_OK,
                      lab_i2c_write(&s_c, 0x23, 0x10, false, cmd + 1, 1, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_UINT(writes + 2u, fake_lab_i2c_writes());
}

/* ---- the dump ------------------------------------------------------------------------------- */

static void test_a_dump_needs_its_arm_and_returns_the_whole_register_file(void)
{
    uint8_t d[256];
    memset(d, 0xEE, sizeof d);
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_i2c_dump(&s_c, 0x68, d, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_i2c_calls());
    TEST_ASSERT_EQUAL_HEX8(0xEE, d[0]);
    /* an arm for another address is not this dump's */
    lab_i2c_arm(&s_c, lab_i2c_fp_dump(0x76));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_i2c_dump(&s_c, 0x68, d, STARK_LAB_I2C_HZ_STD));
    lab_i2c_arm(&s_c, lab_i2c_fp_dump(0x68));
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_dump(&s_c, 0x68, d, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_UINT(16, fake_lab_i2c_calls());
    TEST_ASSERT_EQUAL_HEX8(0x68, d[0x75]);
    TEST_ASSERT_EQUAL_HEX8(0x40, d[0x6b]);
    TEST_ASSERT_EQUAL_HEX8(0x00, d[0x00]);
    TEST_ASSERT_EQUAL_HEX8(0x00, d[0xff]);
    TEST_ASSERT_FALSE(lab_i2c_armed(&s_c));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_i2c_dump(&s_c, 0x68, d, STARK_LAB_I2C_HZ_STD));
    /* a dump is not a write: the register file is as it was */
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_i2c_writes());
}

static void test_a_dump_stops_at_the_first_failure_and_spends_its_arm(void)
{
    uint8_t d[256];
    fake_lab_i2c_fault(FAKE_LAB_I2C_TIMEOUT, 0x68, 1);
    lab_i2c_arm(&s_c, lab_i2c_fp_dump(0x68));
    TEST_ASSERT_EQUAL(STARK_ERR_TIMEOUT, lab_i2c_dump(&s_c, 0x68, d, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_i2c_calls());
    TEST_ASSERT_FALSE(lab_i2c_armed(&s_c));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_i2c_dump(&s_c, 0x68, NULL, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_i2c_dump(&s_c, 0x68, d, 7));
}

static void test_a_module_eeprom_may_be_dumped_and_a_pn532_may_not(void)
{
    put_eeprom(0x50);
    uint8_t d[256];
    lab_i2c_arm(&s_c, lab_i2c_fp_dump(0x50));
    TEST_ASSERT_EQUAL(STARK_OK, lab_i2c_dump(&s_c, 0x50, d, STARK_LAB_I2C_HZ_STD));
    TEST_ASSERT_EQUAL_HEX8(0xA5, d[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA5, d[255]);
}

/* ---- fingerprints and hex ------------------------------------------------------------------- */

static void test_every_fingerprint_matches_the_independent_fnv(void)
{
    TEST_ASSERT_TRUE(FAKE_LAB_FP_VECS >= 12u);
    for (size_t i = 0; i < FAKE_LAB_FP_VECS; i++) {
        const fake_lab_fp_vec_t *v = &fake_lab_fp_vecs[i];
        uint32_t got = v->kind == 'W'
                           ? lab_i2c_fp_write(v->addr, v->reg, v->reg16 != 0u, v->data, v->n)
                           : lab_i2c_fp_dump(v->addr);
        TEST_ASSERT_EQUAL_HEX32(v->fp, got);
        /* the service's names for the same two functions */
        uint32_t svc =
            v->kind == 'W'
                ? stark_lab_i2c_write_fingerprint(v->addr, v->reg, v->reg16 != 0u, v->data, v->n)
                : stark_lab_i2c_dump_fingerprint(v->addr);
        TEST_ASSERT_EQUAL_HEX32(v->fp, svc);
    }
    /* a NULL payload with a count hashes as the count alone, never reads it */
    TEST_ASSERT_EQUAL_HEX32(lab_i2c_fp_write(0x68, 0, false, NULL, 0),
                            lab_i2c_fp_write(0x68, 0, false, NULL, 0));
}

static void test_every_fingerprint_is_distinct(void)
{
    for (size_t i = 0; i < FAKE_LAB_FP_VECS; i++) {
        for (size_t j = i + 1u; j < FAKE_LAB_FP_VECS; j++) {
            TEST_ASSERT_NOT_EQUAL(fake_lab_fp_vecs[i].fp, fake_lab_fp_vecs[j].fp);
        }
    }
}

static void test_every_hex_vector_matches(void)
{
    TEST_ASSERT_TRUE(FAKE_LAB_HEX_VECS >= 100u);
    for (size_t i = 0; i < FAKE_LAB_HEX_VECS; i++) {
        const fake_lab_hex_vec_t *v = &fake_lab_hex_vecs[i];
        char buf[64];
        memset(buf, 'X', sizeof buf);
        size_t len = v->spaced ? lab_i2c_hex(v->data, v->n, buf, v->sz)
                               : lab_i2c_hex_packed(v->data, v->n, buf, v->sz);
        TEST_ASSERT_EQUAL_UINT(strlen(v->text), len);
        if (v->sz == 0u) {
            TEST_ASSERT_EQUAL_CHAR('X', buf[0]); /* nothing is written to a buffer of no size */
        } else {
            TEST_ASSERT_EQUAL_STRING(v->text, buf);
        }
        char svc[64];
        size_t slen = v->spaced ? stark_lab_i2c_hex(v->data, v->n, svc, v->sz)
                                : stark_lab_i2c_hex_packed(v->data, v->n, svc, v->sz);
        TEST_ASSERT_EQUAL_UINT(len, slen);
    }
}

/* ---- the hints ------------------------------------------------------------------------------ */

static void test_the_hints_table_is_small_sorted_generic_and_hedged(void)
{
    size_t n = lab_i2c_hints_count();
    TEST_ASSERT_TRUE(n > 0u && n <= LAB_I2C_HINTS_MAX);
    TEST_ASSERT_EQUAL_UINT(48, LAB_I2C_HINTS_MAX);
    for (size_t i = 0; i < n; i++) {
        uint8_t addr = lab_i2c_hints_addr(i);
        TEST_ASSERT_TRUE(addr >= 0x08u && addr <= 0x77u);
        if (i > 0u) {
            TEST_ASSERT_TRUE_MESSAGE(addr > lab_i2c_hints_addr(i - 1u), "sorted, no duplicates");
        }
        const char *phrase = stark_lab_i2c_hint(addr);
        TEST_ASSERT_NOT_NULL(phrase);
        TEST_ASSERT_EQUAL_INT(0, strncmp(phrase, "may be ", 7));
        TEST_ASSERT_TRUE_MESSAGE(strlen(phrase) <= 30u, phrase); /* fits a row beside the address */
        /* the phrase is data and nothing else: no digits that could pass for a register or a key */
    }
    TEST_ASSERT_EQUAL_UINT8(0, lab_i2c_hints_addr(n));
    TEST_ASSERT_EQUAL_UINT8(0, lab_i2c_hints_addr(n + 100u));
}

static void test_a_hint_is_a_lookup_by_address_and_probes_nothing(void)
{
    unsigned bus = fake_lab_i2c_calls();
    TEST_ASSERT_NOT_NULL(stark_lab_i2c_hint(0x23));
    TEST_ASSERT_NOT_NULL(stark_lab_i2c_hint(0x24));
    TEST_ASSERT_NOT_NULL(stark_lab_i2c_hint(0x3c));
    TEST_ASSERT_NOT_NULL(stark_lab_i2c_hint(0x50));
    TEST_ASSERT_NOT_NULL(stark_lab_i2c_hint(0x68));
    TEST_ASSERT_NOT_NULL(stark_lab_i2c_hint(0x76));
    TEST_ASSERT_NULL(stark_lab_i2c_hint(0x08));
    TEST_ASSERT_NULL(stark_lab_i2c_hint(0x7f));
    TEST_ASSERT_NULL(stark_lab_i2c_hint(0x00));
    TEST_ASSERT_EQUAL_UINT(bus, fake_lab_i2c_calls());
    TEST_ASSERT_NOT_NULL(strstr(stark_lab_i2c_hint(0x50), "module ID"));
}

/* ---- the transport's own argument checks ------------------------------------------------------
 */

static void test_the_transport_checks_its_arguments(void)
{
    uint8_t r[17];
    const uint8_t w[11] = {0};
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_i2c_probe(0x23, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_i2c_probe(0x23, 1001));
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_i2c_probe(0x23, 1000));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_i2c_xfer(
                                                 0x23, STARK_LAB_I2C_HZ_STD, NULL, 0, NULL, 0, 5));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_i2c_xfer(0x23, STARK_LAB_I2C_HZ_STD, w, 11, NULL, 0, 5));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_i2c_xfer(0x23, STARK_LAB_I2C_HZ_STD, w, 1, r, 17, 5));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_i2c_xfer(0x23, STARK_LAB_I2C_HZ_STD, NULL, 1, r, 1, 5));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_i2c_xfer(0x23, STARK_LAB_I2C_HZ_STD, w, 1, NULL, 1, 5));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, stark_lab_transport_i2c_xfer(0x23, 0, w, 1, r, 1, 5));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_i2c_xfer(0x23, 123, w, 1, r, 1, 5));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_i2c_xfer(0x23, STARK_LAB_I2C_HZ_STD, w, 1, r, 1, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_i2c_xfer(0x23, STARK_LAB_I2C_HZ_STD, w, 1, r, 1, 1001));
    /* a receive-only and a write-only transaction are fine */
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_lab_transport_i2c_xfer(0x23, STARK_LAB_I2C_HZ_STD, NULL, 0, r, 2, 5));
    TEST_ASSERT_EQUAL(STARK_OK,
                      stark_lab_transport_i2c_xfer(0x23, STARK_LAB_I2C_HZ_FAST, w, 10, NULL, 0, 5));
    TEST_ASSERT_FALSE(stark_lab_i2c_is_module_eeprom(&s_map, 0x52));
    /* a board that has no EEPROM address (0) never matches address 0 */
    s_map.module[1].eeprom_addr = 0;
    TEST_ASSERT_FALSE(stark_lab_i2c_is_module_eeprom(&s_map, 0));
    TEST_ASSERT_TRUE(stark_lab_i2c_is_module_eeprom(&s_map, 0x50));
    TEST_ASSERT_FALSE(stark_lab_i2c_is_module_eeprom(&s_map, 0x51));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_sweep_vector_matches_the_independent_reference);
    RUN_TEST(test_the_sweep_is_bounded_whatever_the_bus_does);
    RUN_TEST(test_the_scan_finds_exactly_the_five_devices);
    RUN_TEST(test_the_scan_follows_the_device_mask);
    RUN_TEST(test_the_module_eeproms_and_the_pn532_join_the_one_bus);
    RUN_TEST(test_a_timeout_at_one_address_loses_only_it);
    RUN_TEST(test_a_device_that_vanishes_is_found_and_then_gone);
    RUN_TEST(test_who_am_i_and_the_other_fake_devices);
    RUN_TEST(test_a_read_of_an_address_nobody_has_is_a_nack);
    RUN_TEST(test_a_module_eeprom_reads_what_is_in_it);
    RUN_TEST(test_addresses_outside_0x08_to_0x77_are_invalid);
    RUN_TEST(test_read_lengths_registers_and_speeds);
    RUN_TEST(test_a_16_bit_register_goes_out_high_byte_first);
    RUN_TEST(test_every_refusal_vector_matches_the_independent_table);
    RUN_TEST(test_refusal_texts);
    RUN_TEST(test_a_write_to_a_module_eeprom_is_refused_with_no_traffic);
    RUN_TEST(test_the_transport_refuses_what_the_core_would_if_it_were_bypassed);
    RUN_TEST(test_the_pn532_is_refused_by_the_core_while_nfc_is_confirmed);
    RUN_TEST(test_a_16_bit_read_of_a_module_eeprom_is_a_write_and_refused);
    RUN_TEST(test_a_write_needs_its_arm_and_one_arm_buys_one_write);
    RUN_TEST(test_a_different_payload_is_not_what_was_confirmed);
    RUN_TEST(test_a_stale_or_replaced_or_cleared_arm_does_not_write);
    RUN_TEST(test_write_lengths_registers_and_arguments);
    RUN_TEST(test_a_write_to_a_read_only_register_does_not_change_it);
    RUN_TEST(test_a_command_write_to_the_display_and_light_sensor_is_taken);
    RUN_TEST(test_a_dump_needs_its_arm_and_returns_the_whole_register_file);
    RUN_TEST(test_a_dump_stops_at_the_first_failure_and_spends_its_arm);
    RUN_TEST(test_a_module_eeprom_may_be_dumped_and_a_pn532_may_not);
    RUN_TEST(test_every_fingerprint_matches_the_independent_fnv);
    RUN_TEST(test_every_fingerprint_is_distinct);
    RUN_TEST(test_every_hex_vector_matches);
    RUN_TEST(test_the_hints_table_is_small_sorted_generic_and_hedged);
    RUN_TEST(test_a_hint_is_a_lookup_by_address_and_probes_nothing);
    RUN_TEST(test_the_transport_checks_its_arguments);
    return UNITY_END();
}
