/*
 * test_lab_spi.c — the SPI probe's rules (components/stark_lab/lab_spi.c, TASKS.md STARK-0706,
 * ADR-0023 §6) over the real module core and fake 24C02 (the slot states and the lease), the
 * transport's own guard (the host shim of lab_transport_spi.c), the test-only SPI devices
 * (test/fake/fake_lab.c) and the fake CC1101, against the independent vectors of
 * scripts/gen_lab_vectors.py: which slot may be probed and why not (every state against every
 * declared-bit mix), the fingerprint, the single-use request (consumed before the bus is touched,
 * cancelled, stale, changed in any field or by a different module in the slot, replaced), the
 * claim released after success, failure, a refusal and a stale lease, a busy slot, and that no path
 * reaches a driver-owned slot: with a CC1101 module in slot 0 the fake chip sees no transfer, its
 * PATABLE and MARCSTATE do not move. Proves rules and protocol over a model, never a device on a
 * bus (ROADMAP V1 exit 13).
 */
#include <stdio.h>
#include <string.h>
#include "fake_24c02.h"
#include "fake_cc1101.h"
#include "fake_lab.h"
#include "fake_lab_spi_vectors.h"
#include "fake_module_catalog.h"
#include "lab_spi.h"
#include "module_core.h"
#include "unity.h"

#define A0 0x50
#define A1 0x51

/* HARDWARE.md §2's map: slot 0 has a chip select, slot 1 has none. */
static const stark_board_pins_t k_map = {
    .lab_uart_tx = 38,
    .lab_uart_rx = 39,
    .module = {{.cs = 40, .irq0 = 41, .irq1 = 42, .en = -1, .eeprom_addr = A0},
               {.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1, .eeprom_addr = A1}},
};
static const module_wiring_t k_wires[] = {{A0, 40, 41, 42}, {A1, -1, -1, -1}};

static module_core_t s_mod;
static lab_spi_t s_spi;

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

/* What the transport's host shim reads. */
size_t stark_module_list(stark_module_entry_t *out, size_t max)
{
    return module_core_list(&s_mod, out, max);
}

bool stark_module_lease_valid(const stark_module_lease_t *l)
{
    return module_core_lease_valid(&s_mod, l);
}

/* ---- the world around the rules ------------------------------------------------------------- */

static struct {
    char log[24][LAB_SPI_LOG_MAX];
    bool warn[24];
    size_t n_log;
    unsigned claims, releases, dones, xfers;
    uint32_t now_ms;
    stark_err_t claim_fail;
    void (*after_claim)(stark_module_lease_t *l);
    size_t list_over; /* io.list reports this many entries (0: the truth) */
} s_w;

static size_t io_list(void *ctx, stark_module_entry_t *out, size_t max)
{
    (void)ctx;
    if (s_w.list_over != 0u) {
        memset(out, 0, max * sizeof(*out));
        return s_w.list_over;
    }
    return module_core_list(&s_mod, out, max);
}

static stark_err_t io_claim(void *ctx, uint8_t slot, stark_module_lease_t *out)
{
    (void)ctx;
    s_w.claims++;
    if (s_w.claim_fail != STARK_OK) {
        return s_w.claim_fail;
    }
    stark_err_t err = module_core_claim(&s_mod, slot, STARK_CAP_MOD_SPI, LAB_SPI_OWNER, out);
    if (err == STARK_OK && s_w.after_claim != NULL) {
        s_w.after_claim(out);
    }
    return err;
}

static void io_release(void *ctx, const stark_module_lease_t *l)
{
    (void)ctx;
    s_w.releases++;
    module_core_release(&s_mod, l);
}

static stark_err_t io_xfer(void *ctx, const stark_module_lease_t *l, uint32_t hz, uint8_t mode,
                           const uint8_t *tx, uint8_t *rx, size_t n)
{
    (void)ctx;
    s_w.xfers++;
    return stark_lab_transport_spi_xfer(l, hz, mode, tx, rx, n);
}

static uint32_t io_now(void *ctx)
{
    (void)ctx;
    return s_w.now_ms;
}

static void io_log(void *ctx, bool warn, const char *text)
{
    (void)ctx;
    if (s_w.n_log < 24u) {
        snprintf(s_w.log[s_w.n_log], LAB_SPI_LOG_MAX, "%s", text);
        s_w.warn[s_w.n_log] = warn;
        s_w.n_log++;
    }
}

static void io_done(void *ctx)
{
    (void)ctx;
    s_w.dones++;
}

static const lab_spi_io_t k_io = {.list = io_list,
                                  .claim = io_claim,
                                  .release = io_release,
                                  .xfer = io_xfer,
                                  .now_ms = io_now,
                                  .log = io_log,
                                  .done = io_done};

static void ticks(int n)
{
    for (int i = 0; i < n; i++) {
        (void)module_core_tick(&s_mod);
    }
}

static void fit(uint8_t addr, int id)
{
    fake_24c02_put(addr, k_fake_modules[id - 1].image, MODULE_DESC_SIZE);
    ticks(3);
}

static void unfit(uint8_t addr)
{
    fake_24c02_remove(addr);
    ticks(3);
}

void setUp(void)
{
    memset(&s_w, 0, sizeof(s_w));
    s_w.now_ms = 1000;
    fake_24c02_reset();
    fake_lab_reset();
    fake_cc1101_reset();
    module_core_init(&s_mod, &k_mio, k_wires, 2);
    ticks(2);
    lab_spi_init(&s_spi, &k_io, &k_map);
}

void tearDown(void)
{}

static bool logged(const char *needle)
{
    for (size_t i = 0; i < s_w.n_log; i++) {
        if (strstr(s_w.log[i], needle) != NULL) {
            return true;
        }
    }
    return false;
}

static const char *last_log(void)
{
    return s_w.n_log > 0u ? s_w.log[s_w.n_log - 1u] : "";
}

/* True when slot 0 is free: a claim by anyone else succeeds (and is dropped again). */
static bool slot0_free(void)
{
    stark_module_lease_t l;
    stark_err_t err = module_core_claim(&s_mod, 0, STARK_CAP_MOD_SPI, "other", &l);
    if (err == STARK_OK) {
        module_core_release(&s_mod, &l);
    }
    return err == STARK_OK;
}

static const uint8_t k_jedec[4] = {0x9f, 0x00, 0x00, 0x00};

static stark_err_t request_jedec(stark_lab_spi_req_t *r)
{
    return lab_spi_request(&s_spi, 0, 1000000u, 0, k_jedec, sizeof k_jedec, r);
}

/* ---- eligibility ----------------------------------------------------------------------------- */

static void test_every_judge_vector_matches_the_independent_reference(void)
{
    TEST_ASSERT_TRUE(FAKE_LAB_SPI_JUDGE_VECS >= 150u);
    for (size_t i = 0; i < FAKE_LAB_SPI_JUDGE_VECS; i++) {
        const fake_lab_spi_judge_vec_t *v = &fake_lab_spi_judge_vecs[i];
        stark_module_entry_t e;
        memset(&e, 0, sizeof e);
        e.kind = STARK_MODULE_SLOT;
        e.state = (stark_module_state_t)v->state;
        e.declared = v->declared;
        e.unsupported = v->unsupported != 0u;
        snprintf(e.name, sizeof e.name, "Mod %u", (unsigned)i % 100u);
        stark_lab_spi_slot_t s;
        lab_spi_judge(&e, v->cs != 0u, 0, &s);
        char msg[48];
        snprintf(msg, sizeof msg, "vector %u", (unsigned)i);
        TEST_ASSERT_EQUAL_MESSAGE(v->eligible, s.eligible ? 1 : 0, msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(v->why, s.why, msg);
        /* the transport's guard is the same rule (with the chip select judged by the lease) */
        TEST_ASSERT_EQUAL_MESSAGE(v->cs != 0u && stark_lab_spi_entry_ok(&e), s.eligible, msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(e.state == STARK_MODULE_ABSENT ? "" : e.name, s.name, msg);
    }
}

static void test_the_judge_names_a_slot_the_table_does_not_have(void)
{
    stark_lab_spi_slot_t s;
    lab_spi_judge(NULL, true, 1, &s);
    TEST_ASSERT_FALSE(s.eligible);
    TEST_ASSERT_EQUAL_STRING("no module", s.why);
    TEST_ASSERT_EQUAL_UINT8(1, s.slot);
    lab_spi_judge(NULL, false, 0, &s);
    TEST_ASSERT_EQUAL_STRING("no chip select", s.why);
}

static void test_the_guard_needs_a_slot_entry(void)
{
    stark_module_entry_t list[2];
    memset(list, 0, sizeof list);
    list[0].kind = STARK_MODULE_SLOT;
    list[0].slot = 0;
    list[0].state = STARK_MODULE_KNOWN;
    list[0].declared = STARK_CAP_MOD_SPI;
    list[1].kind = STARK_MODULE_BUS; /* a provider is never a slot */
    list[1].state = STARK_MODULE_KNOWN;
    list[1].declared = STARK_CAP_MOD_SPI;
    TEST_ASSERT_TRUE(stark_lab_spi_slot_ok(list, 2, 0));
    TEST_ASSERT_FALSE(stark_lab_spi_slot_ok(list, 2, 1));
    TEST_ASSERT_FALSE(stark_lab_spi_slot_ok(list, 2, -1));
    TEST_ASSERT_FALSE(stark_lab_spi_slot_ok(list, 0, 0));
    list[1].kind = STARK_MODULE_ONBOARD;
    TEST_ASSERT_FALSE(stark_lab_spi_entry_ok(&list[1]));
}

static void test_slots_with_an_empty_board(void)
{
    stark_lab_spi_slot_t s[2];
    TEST_ASSERT_EQUAL_UINT(2, lab_spi_slots(&s_spi, s, 2));
    TEST_ASSERT_EQUAL_UINT8(0, s[0].slot);
    TEST_ASSERT_FALSE(s[0].eligible);
    TEST_ASSERT_EQUAL_STRING("no module", s[0].why);
    TEST_ASSERT_EQUAL_STRING("", s[0].name);
    TEST_ASSERT_EQUAL_UINT8(1, s[1].slot);
    TEST_ASSERT_FALSE(s[1].eligible);
    TEST_ASSERT_EQUAL_STRING("no chip select", s[1].why);
}

static void test_slots_for_each_module_in_the_catalogue(void)
{
    struct {
        int id;
        bool ok;
        const char *why;
    } k[] = {
        {FAKE_MODULE_SPI_BREAKOUT, true, ""},
        {FAKE_MODULE_SPI_ONLY, true, ""},
        {FAKE_MODULE_SPI_IRQ, true, ""}, /* MOD_SPI and MOD_IRQ: no function */
        {FAKE_MODULE_CC1101_MODULE, false, "owned by Sub-GHz"},
        {FAKE_MODULE_NFC_CARRIER, false, "owned by NFC"},
        {FAKE_MODULE_ALL_KNOWN_SLOT, false, "owned by NFC"},
        {FAKE_MODULE_NO_CAPS, false, "no SPI declared"},
        {FAKE_MODULE_TWIN_A, false, "no SPI declared"}, /* MOD_IRQ only */
        {FAKE_MODULE_UNKNOWN_BIT_6, false, "owned by unknown"},
        {FAKE_MODULE_UNKNOWN_BITS, false, "unsupported module"},
        {FAKE_MODULE_UNSUP_IR, false, "unsupported module"},
        {FAKE_MODULE_BLANK, false, "generic module"},
        {FAKE_MODULE_BAD_CRC, false, "generic module"},
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        setUp();
        fit(A0, k[i].id);
        stark_lab_spi_slot_t s[2];
        TEST_ASSERT_EQUAL_UINT(2, lab_spi_slots(&s_spi, s, 2));
        TEST_ASSERT_EQUAL_MESSAGE(k[i].ok, s[0].eligible, k_fake_modules[k[i].id - 1].label);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(k[i].why, s[0].why, k_fake_modules[k[i].id - 1].label);
        TEST_ASSERT_FALSE(s[1].eligible); /* slot 1 has no chip select, whatever is in it */
        TEST_ASSERT_EQUAL_STRING("no chip select", s[1].why);
    }
    setUp();
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    stark_lab_spi_slot_t s[2];
    (void)lab_spi_slots(&s_spi, s, 2);
    TEST_ASSERT_EQUAL_STRING("SPI breakout", s[0].name);
}

static void test_two_nfc_modules_conflict(void)
{
    fit(A0, FAKE_MODULE_NFC_CARRIER);
    fit(A1, FAKE_MODULE_NFC_RIVAL);
    stark_lab_spi_slot_t s[2];
    (void)lab_spi_slots(&s_spi, s, 2);
    TEST_ASSERT_FALSE(s[0].eligible);
    TEST_ASSERT_EQUAL_STRING("conflict", s[0].why);
}

static void test_slots_takes_at_most_what_fits(void)
{
    stark_lab_spi_slot_t s[2];
    TEST_ASSERT_EQUAL_UINT(0, lab_spi_slots(&s_spi, s, 0));
    TEST_ASSERT_EQUAL_UINT(1, lab_spi_slots(&s_spi, s, 1));
    TEST_ASSERT_EQUAL_UINT(2, lab_spi_slots(&s_spi, s, 7));
}

static void test_a_table_that_claims_too_many_entries_is_clamped(void)
{
    s_w.list_over = STARK_MODULE_ENTRIES + 5u;
    stark_lab_spi_slot_t s[2];
    TEST_ASSERT_EQUAL_UINT(2, lab_spi_slots(&s_spi, s, 2));
    TEST_ASSERT_EQUAL_STRING("no module", s[0].why);
}

static void test_the_wiring_comes_from_the_pin_map(void)
{
    TEST_ASSERT_TRUE(s_spi.cs[0]);
    TEST_ASSERT_FALSE(s_spi.cs[1]);
}

/* ---- the fingerprint --------------------------------------------------------------------------
 */

static void test_every_fingerprint_vector_matches_the_independent_reference(void)
{
    TEST_ASSERT_TRUE(FAKE_LAB_SPI_FP_VECS >= 14u);
    for (size_t i = 0; i < FAKE_LAB_SPI_FP_VECS; i++) {
        const fake_lab_spi_fp_vec_t *v = &fake_lab_spi_fp_vecs[i];
        stark_module_entry_t e;
        memset(&e, 0, sizeof e);
        e.id = v->id;
        e.rev = v->rev;
        e.declared = v->declared;
        TEST_ASSERT_EQUAL_HEX32(v->fp, lab_spi_fp(v->slot, v->hz, v->mode, v->tx, v->n, &e));
    }
}

static void test_a_fingerprint_without_a_module_or_bytes_is_defined(void)
{
    TEST_ASSERT_NOT_EQUAL(lab_spi_fp(0, 1000000u, 0, k_jedec, 4, NULL),
                          lab_spi_fp(0, 1000000u, 0, NULL, 4, NULL));
    TEST_ASSERT_EQUAL_HEX32(lab_spi_fp(0, 1000000u, 0, NULL, 0, NULL),
                            lab_spi_fp(0, 1000000u, 0, k_jedec, 0, NULL));
}

/* ---- the request ------------------------------------------------------------------------------
 */

static void test_a_request_on_an_eligible_slot_arms_and_touches_no_bus(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    stark_lab_spi_req_t r;
    memset(&r, 0xAA, sizeof r);
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    TEST_ASSERT_TRUE(lab_spi_armed(&s_spi));
    TEST_ASSERT_EQUAL_UINT8(0, r.slot);
    TEST_ASSERT_EQUAL_UINT32(1000000u, r.hz);
    TEST_ASSERT_EQUAL_UINT8(0, r.mode);
    TEST_ASSERT_EQUAL_UINT8(4, r.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(k_jedec, r.tx, 4);
    for (size_t i = 4; i < STARK_LAB_SPI_MAX; i++) {
        TEST_ASSERT_EQUAL_HEX8(0, r.tx[i]); /* the rest of the request is clean */
    }
    TEST_ASSERT_EQUAL_UINT(0, s_w.claims);
    TEST_ASSERT_EQUAL_UINT(0, s_w.xfers);
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
}

static void test_every_clock_and_mode_and_length_at_each_edge_is_accepted(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    uint8_t tx[STARK_LAB_SPI_MAX];
    memset(tx, 0x5A, sizeof tx);
    stark_lab_spi_req_t r;
    for (size_t c = 0; c < STARK_LAB_SPI_CLOCKS; c++) {
        for (uint8_t mode = 0; mode < 4; mode++) {
            TEST_ASSERT_EQUAL(STARK_OK,
                              lab_spi_request(&s_spi, 0, stark_lab_spi_clock(c), mode, tx, 1, &r));
            TEST_ASSERT_EQUAL(STARK_OK, lab_spi_request(&s_spi, 0, stark_lab_spi_clock(c), mode, tx,
                                                        STARK_LAB_SPI_MAX, &r));
            TEST_ASSERT_EQUAL_UINT8(STARK_LAB_SPI_MAX, r.n);
        }
    }
}

static void test_a_bad_request_is_invalid_and_leaves_no_arm(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    uint8_t tx[STARK_LAB_SPI_MAX + 1] = {0};
    stark_lab_spi_req_t r;
    struct {
        uint8_t slot;
        uint32_t hz;
        uint8_t mode;
        const uint8_t *tx;
        size_t n;
        bool null_out;
    } bad[] = {
        {2, 1000000u, 0, tx, 4, false},   /* a slot that does not exist */
        {0, 1000000u, 0, tx, 0, false},   /* no bytes */
        {0, 1000000u, 0, tx, 33, false},  /* too many */
        {0, 1000000u, 0, NULL, 4, false}, /* no data */
        {0, 1000000u, 0, tx, 4, true},    /* nowhere to put the request */
        {0, 0u, 0, tx, 4, false},         /* clock 0 */
        {0, 99999u, 0, tx, 4, false},     /* between the list */
        {0, 750000u, 0, tx, 4, false},    /* between the list */
        {0, 4000001u, 0, tx, 4, false},   /* above it */
        {0, 8000000u, 0, tx, 4, false},   /* far above it */
        {0, 1000000u, 4, tx, 4, false},   /* mode past 3 */
        {0, 1000000u, 255, tx, 4, false},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r)); /* an earlier arm ... */
        TEST_ASSERT_TRUE(lab_spi_armed(&s_spi));
        TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                          lab_spi_request(&s_spi, bad[i].slot, bad[i].hz, bad[i].mode, bad[i].tx,
                                          bad[i].n, bad[i].null_out ? NULL : &r));
        TEST_ASSERT_FALSE_MESSAGE(lab_spi_armed(&s_spi), "a bad request clears the arm");
    }
}

static void test_a_request_on_a_driver_owned_slot_is_refused_with_the_reason(void)
{
    fit(A0, FAKE_MODULE_CC1101_MODULE);
    stark_lab_spi_req_t r;
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, request_jedec(&r));
    TEST_ASSERT_FALSE(lab_spi_armed(&s_spi));
    TEST_ASSERT_TRUE(logged("spi request refused: owned by Sub-GHz"));
    TEST_ASSERT_TRUE(s_w.warn[s_w.n_log - 1u]);
    /* slot 1 has no chip select, so no module there is ever probed */
    fit(A1, FAKE_MODULE_SPI_BREAKOUT);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      lab_spi_request(&s_spi, 1, 1000000u, 0, k_jedec, 4, &r));
    TEST_ASSERT_TRUE(logged("spi request refused: no chip select"));
    unfit(A0);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, request_jedec(&r));
    TEST_ASSERT_TRUE(logged("spi request refused: no module"));
}

/* ---- the send ---------------------------------------------------------------------------------
 */

static void test_a_confirmed_jedec_transaction_happens_once_and_is_logged(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    fake_lab_spi_device(FAKE_LAB_SPI_FLASH);
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_send(&s_spi, &r, rx));
    const uint8_t want[4] = {0x00, 0xef, 0x40, 0x18};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, rx, 4);
    TEST_ASSERT_EQUAL_STRING("spi slot 0 1 MHz mode 0 tx 9f 00 00 00 rx 00 ef 40 18", last_log());
    TEST_ASSERT_FALSE(s_w.warn[s_w.n_log - 1u]);
    TEST_ASSERT_EQUAL_UINT(1, s_w.claims);
    TEST_ASSERT_EQUAL_UINT(1, s_w.releases);
    TEST_ASSERT_EQUAL_UINT(1, s_w.dones);
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_spi_last_slot());
    TEST_ASSERT_EQUAL_UINT32(1000000u, fake_lab_spi_last_hz());
    TEST_ASSERT_EQUAL_UINT8(0, fake_lab_spi_last_mode());
    TEST_ASSERT_EQUAL_UINT(4, fake_lab_spi_last_len());
    TEST_ASSERT_TRUE_MESSAGE(slot0_free(), "no lease outlives a transaction");
    TEST_ASSERT_FALSE(lab_spi_armed(&s_spi));
}

static void test_one_confirmation_buys_one_transaction(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_send(&s_spi, &r, rx));
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_spi_send(&s_spi, &r, rx)); /* the same request */
    }
    TEST_ASSERT_TRUE(logged("spi send refused: not confirmed"));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL_UINT(1, s_w.claims);
}

static void test_the_request_is_consumed_before_the_bus_is_touched(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    fake_lab_fault(FAKE_LAB_FAULT_SPI_ERROR, -1, -1); /* the transfer fails */
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    TEST_ASSERT_EQUAL(STARK_ERR_IO, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_TRUE(logged("tx 9f 00 00 00 failed:"));
    TEST_ASSERT_TRUE(s_w.warn[s_w.n_log - 1u]);
    TEST_ASSERT_FALSE_MESSAGE(lab_spi_armed(&s_spi), "a failure leaves no arm");
    TEST_ASSERT_TRUE_MESSAGE(slot0_free(), "a failure releases the lease");
    TEST_ASSERT_EQUAL_UINT(1, s_w.dones);
    fake_lab_fault(FAKE_LAB_FAULT_NONE, -1, 0);
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_spi_send(&s_spi, &r, rx)); /* no retry on the old OK */
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_spi_calls());
    for (size_t i = 0; i < r.n; i++) {
        TEST_ASSERT_EQUAL_HEX8(0, rx[i]); /* a failed or refused send hands back zeros */
    }
}

static void test_no_answer_is_a_result_not_an_error(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    fake_lab_spi_device(FAKE_LAB_SPI_FLASH);
    fake_lab_fault(FAKE_LAB_FAULT_SPI_NO_ANSWER, -1, 1);
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_EQUAL_STRING("spi slot 0 1 MHz mode 0 tx 9f 00 00 00 rx ff ff ff ff", last_log());
}

static void test_an_echo_and_the_other_clocks_and_modes_are_logged(void)
{
    fit(A0, FAKE_MODULE_SPI_ONLY);
    fake_lab_spi_device(FAKE_LAB_SPI_ECHO);
    const uint8_t tx[3] = {0xa5, 0x5a, 0x01};
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_request(&s_spi, 0, 250000u, 3, tx, 3, &r));
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_EQUAL_STRING("spi slot 0 250 kHz mode 3 tx a5 5a 01 rx 00 a5 5a", last_log());
    TEST_ASSERT_EQUAL_UINT8(3, fake_lab_spi_last_mode());
    TEST_ASSERT_EQUAL_UINT32(250000u, fake_lab_spi_last_hz());
}

static void test_the_longest_transaction_fits_the_log_line(void)
{
    fit(A0, FAKE_MODULE_SPI_IRQ);
    fake_lab_spi_device(FAKE_LAB_SPI_ECHO);
    uint8_t tx[STARK_LAB_SPI_MAX];
    for (size_t i = 0; i < sizeof tx; i++) {
        tx[i] = (uint8_t)(0xE0u + i);
    }
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_request(&s_spi, 0, 4000000u, 2, tx, STARK_LAB_SPI_MAX, &r));
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_send(&s_spi, &r, rx));
    const char *line = last_log();
    TEST_ASSERT_NOT_NULL(strstr(line, "spi slot 0 4 MHz mode 2 tx e0 e1 e2"));
    TEST_ASSERT_NOT_NULL(strstr(line, " rx 00 e0 e1 "));
    TEST_ASSERT_TRUE(strlen(line) > 190u && strlen(line) < LAB_SPI_LOG_MAX);
    TEST_ASSERT_EQUAL_HEX8(0xFE, rx[STARK_LAB_SPI_MAX - 1u]); /* the echo of byte 30 */
}

static void test_send_without_a_request_or_after_a_cancel_is_refused(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    lab_spi_cancel(&s_spi);
    TEST_ASSERT_FALSE(lab_spi_armed(&s_spi));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_spi_send(&s_spi, &r, rx)); /* nothing was ever armed */
    TEST_ASSERT_EQUAL_UINT(0, s_w.claims);
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
}

static void test_send_with_bad_arguments_still_spends_the_arm(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_spi_send(&s_spi, NULL, rx));
    TEST_ASSERT_FALSE(lab_spi_armed(&s_spi));
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_spi_send(&s_spi, &r, NULL));
    TEST_ASSERT_FALSE(lab_spi_armed(&s_spi));
    stark_lab_spi_req_t bad = r;
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    bad.n = 33;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_spi_send(&s_spi, &bad, rx));
    TEST_ASSERT_FALSE(lab_spi_armed(&s_spi));
    bad = r;
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    bad.hz = 123456u;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_spi_send(&s_spi, &bad, rx));
    bad = r;
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    bad.mode = 9;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_spi_send(&s_spi, &bad, rx));
    bad = r;
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    bad.slot = 2;
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, lab_spi_send(&s_spi, &bad, rx));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL_UINT(0, s_w.claims);
}

static void test_a_request_goes_stale_after_30_seconds(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    s_w.now_ms += LAB_SPI_ARM_MS;
    TEST_ASSERT_EQUAL_MESSAGE(STARK_OK, lab_spi_send(&s_spi, &r, rx), "30 s is still fresh");
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    s_w.now_ms += LAB_SPI_ARM_MS + 1u;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_spi_calls());
    /* the clock wraps */
    s_w.now_ms = 0xFFFFFF00u;
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    s_w.now_ms += 1000u; /* wraps past zero */
    TEST_ASSERT_TRUE(s_w.now_ms < 0xFFFFFF00u);
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    s_w.now_ms += LAB_SPI_ARM_MS + 1u;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_spi_send(&s_spi, &r, rx));
}

static void test_any_changed_field_is_a_different_transaction(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    uint8_t rx[STARK_LAB_SPI_MAX];
    for (int field = 0; field < 4; field++) {
        stark_lab_spi_req_t r;
        TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
        switch (field) {
            case 0:
                r.tx[3] ^= 1u;
                break;
            case 1:
                r.hz = 2000000u;
                break;
            case 2:
                r.mode = 1;
                break;
            default:
                r.n = 3;
                break;
        }
        TEST_ASSERT_EQUAL_MESSAGE(STARK_ERR_STATE, lab_spi_send(&s_spi, &r, rx), "changed");
        TEST_ASSERT_TRUE(logged("spi send refused: changed"));
        TEST_ASSERT_FALSE(lab_spi_armed(&s_spi));
    }
    TEST_ASSERT_EQUAL_UINT(0, s_w.claims);
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
}

static void test_a_later_request_replaces_an_earlier_one(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    stark_lab_spi_req_t a;
    stark_lab_spi_req_t b;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&a));
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_request(&s_spi, 0, 100000u, 0, k_jedec, 4, &b));
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_spi_send(&s_spi, &a, rx)); /* A is not what was armed */
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_spi_send(&s_spi, &b, rx)); /* and that spent B's arm */
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_request(&s_spi, 0, 100000u, 0, k_jedec, 4, &b));
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_send(&s_spi, &b, rx));
}

static void test_a_different_module_in_the_slot_is_a_different_transaction(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    fit(A0, FAKE_MODULE_SPI_ONLY); /* another eligible module: a new descriptor, a new id */
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_TRUE(logged("spi send refused: changed"));
    TEST_ASSERT_EQUAL_UINT(0, s_w.claims);
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
}

static void test_a_slot_that_stops_being_eligible_is_refused_at_the_send(void)
{
    uint8_t rx[STARK_LAB_SPI_MAX];
    stark_lab_spi_req_t r;
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    fit(A0, FAKE_MODULE_CC1101_MODULE); /* the module is swapped for a radio */
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_TRUE(logged("spi send refused: owned by Sub-GHz"));
    TEST_ASSERT_EQUAL_UINT(0, s_w.claims);
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL_UINT32(0, fake_cc1101_stats()->transfers);

    unfit(A0);
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    unfit(A0); /* the module is pulled */
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_TRUE(logged("spi send refused: no module"));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
}

static void swap_for_a_radio(stark_module_lease_t *l)
{
    (void)l;
    fit(A0, FAKE_MODULE_CC1101_MODULE); /* between the claim and the transaction */
}

static void swap_for_another_breakout(stark_module_lease_t *l)
{
    (void)l;
    fit(A0, FAKE_MODULE_SPI_ONLY);
}

static void test_the_slot_is_judged_again_after_the_claim(void)
{
    uint8_t rx[STARK_LAB_SPI_MAX];
    stark_lab_spi_req_t r;
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    s_w.after_claim = swap_for_a_radio;
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_EQUAL_UINT(1, s_w.claims);
    TEST_ASSERT_EQUAL_UINT(1, s_w.releases);
    TEST_ASSERT_EQUAL_UINT(0, s_w.xfers);
    TEST_ASSERT_EQUAL_UINT(0, s_w.dones);
    TEST_ASSERT_EQUAL_UINT32(0, fake_cc1101_stats()->transfers);

    setUp();
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    s_w.after_claim = swap_for_another_breakout;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_EQUAL_UINT(0, s_w.xfers);
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    TEST_ASSERT_TRUE(slot0_free());
}

static void drop_the_lease(stark_module_lease_t *l)
{
    module_core_release(&s_mod, l); /* the lease is stale before the bus is reached */
}

static void test_a_stale_lease_reaches_nothing_and_is_released(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    s_w.after_claim = drop_the_lease;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_TRUE(logged("failed: Invalid state"));
    TEST_ASSERT_EQUAL_UINT(1, s_w.xfers);
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL_UINT(1, s_w.releases);
    TEST_ASSERT_TRUE(slot0_free());
}

static void test_a_busy_slot_is_reported_and_left_alone(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    stark_module_lease_t other;
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_mod, 0, STARK_CAP_MOD_SPI, "driver", &other));
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r)); /* a request claims nothing */
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_EQUAL_STRING("spi slot 0 busy", last_log());
    TEST_ASSERT_TRUE(module_core_lease_valid(&s_mod, &other));
    TEST_ASSERT_EQUAL_UINT(0, s_w.releases);
    TEST_ASSERT_EQUAL_UINT(0, s_w.dones);
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    module_core_release(&s_mod, &other);
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_send(&s_spi, &r, rx));
}

static void test_another_claim_error_is_passed_on(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    s_w.claim_fail = STARK_ERR_NOT_FOUND;
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, request_jedec(&r));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_TRUE(logged("spi slot 0 Not found"));
    TEST_ASSERT_EQUAL_UINT(0, s_w.releases);
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
}

/* ---- nothing reaches a driver-owned slot --------------------------------------------------- */

static stark_err_t chip_ext(uint8_t slot, const uint8_t *tx, uint8_t *rx, size_t n)
{
    return slot == 0 ? fake_cc1101_xfer(tx, rx, n) : STARK_ERR_NOT_FOUND;
}

static void expect_chip_untouched(void)
{
    const fake_cc1101_stats_t *st = fake_cc1101_stats();
    TEST_ASSERT_EQUAL_UINT32(0, st->transfers);
    TEST_ASSERT_EQUAL_UINT32(0, st->patable_writes);
    TEST_ASSERT_EQUAL_UINT32(0, st->tx_strobes);
    TEST_ASSERT_EQUAL_UINT8(0x01, st->marcstate); /* IDLE, as at power-on */
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
}

/* The writes a raw probe could make to a radio: PATABLE (burst 0x7E), STX and a long burst. */
static const uint8_t k_patable[] = {0x7e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t k_stx[] = {0x35};
static const uint8_t k_all_ff[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static void test_the_radio_sees_no_transfer_whatever_the_probe_does(void)
{
    fake_lab_spi_ext(chip_ext);
    fit(A0, FAKE_MODULE_CC1101_MODULE);
    const struct {
        const uint8_t *tx;
        size_t n;
    } writes[] = {
        {k_patable, sizeof k_patable}, {k_stx, sizeof k_stx}, {k_all_ff, 32}, {k_jedec, 4}};
    uint8_t rx[STARK_LAB_SPI_MAX];
    for (size_t i = 0; i < sizeof writes / sizeof writes[0]; i++) {
        stark_lab_spi_req_t r;
        /* through the service: refused at the request ... */
        TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                          lab_spi_request(&s_spi, 0, 1000000u, 0, writes[i].tx, writes[i].n, &r));
        /* ... and a hand-made request is refused at the send (nothing armed) ... */
        memset(&r, 0, sizeof r);
        r.hz = 1000000u;
        r.n = (uint8_t)writes[i].n;
        memcpy(r.tx, writes[i].tx, writes[i].n);
        TEST_ASSERT_EQUAL(STARK_ERR_STATE, lab_spi_send(&s_spi, &r, rx));
        /* ... and the transport refuses it again even with a lease the radio's driver could hold */
        stark_module_lease_t l;
        TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_mod, 0, STARK_CAP_MOD_SPI, "driver", &l));
        TEST_ASSERT_EQUAL(
            STARK_ERR_NOT_SUPPORTED,
            stark_lab_transport_spi_xfer(&l, 1000000u, 0, writes[i].tx, rx, writes[i].n));
        module_core_release(&s_mod, &l);
    }
    expect_chip_untouched();
    TEST_ASSERT_EQUAL_UINT(0, s_w.claims);
}

static void test_a_swap_to_the_radio_after_the_request_never_reaches_it(void)
{
    fake_lab_spi_ext(chip_ext);
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK,
                      lab_spi_request(&s_spi, 0, 1000000u, 0, k_patable, sizeof k_patable, &r));
    fit(A0, FAKE_MODULE_CC1101_MODULE);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_spi_send(&s_spi, &r, rx));
    expect_chip_untouched();
    s_w.after_claim = NULL;
    unfit(A0);
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_request(&s_spi, 0, 1000000u, 0, k_stx, sizeof k_stx, &r));
    s_w.after_claim = swap_for_a_radio;
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, lab_spi_send(&s_spi, &r, rx));
    expect_chip_untouched();
}

static void test_the_control_the_same_harness_does_reach_a_chip_on_an_eligible_slot(void)
{
    /* NOTE: without this the test above could pass for the wrong reason (a harness that reaches
     * nothing). The same ext binding, an eligible module: the chip answers and counts. */
    fake_lab_spi_ext(chip_ext);
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    stark_lab_spi_req_t r;
    uint8_t rx[STARK_LAB_SPI_MAX];
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_request(&s_spi, 0, 1000000u, 0, k_stx, sizeof k_stx, &r));
    TEST_ASSERT_EQUAL(STARK_OK, lab_spi_send(&s_spi, &r, rx));
    TEST_ASSERT_EQUAL_UINT32(1, fake_cc1101_stats()->transfers);
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_spi_calls());
}

/* ---- the transport's guard on its own ---------------------------------------------------------
 */

static void test_the_transport_checks_its_arguments_and_the_slot(void)
{
    fit(A0, FAKE_MODULE_SPI_BREAKOUT);
    stark_module_lease_t l;
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_mod, 0, STARK_CAP_MOD_SPI, "t", &l));
    uint8_t rx[STARK_LAB_SPI_MAX];
    const uint8_t tx[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_spi_xfer(NULL, 1000000u, 0, tx, rx, 4));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_spi_xfer(&l, 1000000u, 0, NULL, rx, 4));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_spi_xfer(&l, 1000000u, 0, tx, NULL, 4));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_spi_xfer(&l, 1000000u, 0, tx, rx, 0));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_spi_xfer(&l, 1000000u, 0, tx, rx, 33));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_spi_xfer(&l, 1000001u, 0, tx, rx, 4));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG,
                      stark_lab_transport_spi_xfer(&l, 1000000u, 4, tx, rx, 4));
    TEST_ASSERT_EQUAL_UINT(0, fake_lab_spi_calls());
    TEST_ASSERT_EQUAL(STARK_OK, stark_lab_transport_spi_xfer(&l, 1000000u, 0, tx, rx, 4));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_spi_calls());
    module_core_release(&s_mod, &l);
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, stark_lab_transport_spi_xfer(&l, 1000000u, 0, tx, rx, 4));
    stark_module_lease_t nowhere = {.slot = 5, .gen = 0};
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      stark_lab_transport_spi_xfer(&nowhere, 1000000u, 0, tx, rx, 4));
    TEST_ASSERT_EQUAL_UINT(1, fake_lab_spi_calls());
}

/* ---- the helpers the app uses -----------------------------------------------------------------
 */

static void test_the_clock_list_and_its_text(void)
{
    const uint32_t hz[6] = {100000u, 250000u, 500000u, 1000000u, 2000000u, 4000000u};
    const char *text[6] = {"100 kHz", "250 kHz", "500 kHz", "1 MHz", "2 MHz", "4 MHz"};
    for (size_t i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_UINT32(hz[i], stark_lab_spi_clock(i));
        TEST_ASSERT_EQUAL_STRING(text[i], stark_lab_spi_clock_text(hz[i]));
    }
    TEST_ASSERT_EQUAL_UINT32(0, stark_lab_spi_clock(6));
    TEST_ASSERT_EQUAL_STRING("?", stark_lab_spi_clock_text(0));
    TEST_ASSERT_EQUAL_STRING("?", stark_lab_spi_clock_text(3000000u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_judge_vector_matches_the_independent_reference);
    RUN_TEST(test_the_judge_names_a_slot_the_table_does_not_have);
    RUN_TEST(test_the_guard_needs_a_slot_entry);
    RUN_TEST(test_slots_with_an_empty_board);
    RUN_TEST(test_slots_for_each_module_in_the_catalogue);
    RUN_TEST(test_two_nfc_modules_conflict);
    RUN_TEST(test_slots_takes_at_most_what_fits);
    RUN_TEST(test_a_table_that_claims_too_many_entries_is_clamped);
    RUN_TEST(test_the_wiring_comes_from_the_pin_map);
    RUN_TEST(test_every_fingerprint_vector_matches_the_independent_reference);
    RUN_TEST(test_a_fingerprint_without_a_module_or_bytes_is_defined);
    RUN_TEST(test_a_request_on_an_eligible_slot_arms_and_touches_no_bus);
    RUN_TEST(test_every_clock_and_mode_and_length_at_each_edge_is_accepted);
    RUN_TEST(test_a_bad_request_is_invalid_and_leaves_no_arm);
    RUN_TEST(test_a_request_on_a_driver_owned_slot_is_refused_with_the_reason);
    RUN_TEST(test_a_confirmed_jedec_transaction_happens_once_and_is_logged);
    RUN_TEST(test_one_confirmation_buys_one_transaction);
    RUN_TEST(test_the_request_is_consumed_before_the_bus_is_touched);
    RUN_TEST(test_no_answer_is_a_result_not_an_error);
    RUN_TEST(test_an_echo_and_the_other_clocks_and_modes_are_logged);
    RUN_TEST(test_the_longest_transaction_fits_the_log_line);
    RUN_TEST(test_send_without_a_request_or_after_a_cancel_is_refused);
    RUN_TEST(test_send_with_bad_arguments_still_spends_the_arm);
    RUN_TEST(test_a_request_goes_stale_after_30_seconds);
    RUN_TEST(test_any_changed_field_is_a_different_transaction);
    RUN_TEST(test_a_later_request_replaces_an_earlier_one);
    RUN_TEST(test_a_different_module_in_the_slot_is_a_different_transaction);
    RUN_TEST(test_a_slot_that_stops_being_eligible_is_refused_at_the_send);
    RUN_TEST(test_the_slot_is_judged_again_after_the_claim);
    RUN_TEST(test_a_stale_lease_reaches_nothing_and_is_released);
    RUN_TEST(test_a_busy_slot_is_reported_and_left_alone);
    RUN_TEST(test_another_claim_error_is_passed_on);
    RUN_TEST(test_the_radio_sees_no_transfer_whatever_the_probe_does);
    RUN_TEST(test_a_swap_to_the_radio_after_the_request_never_reaches_it);
    RUN_TEST(test_the_control_the_same_harness_does_reach_a_chip_on_an_eligible_slot);
    RUN_TEST(test_the_transport_checks_its_arguments_and_the_slot);
    RUN_TEST(test_the_clock_list_and_its_text);
    return UNITY_END();
}
