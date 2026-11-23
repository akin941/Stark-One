/*
 * test_subghz_service.c — the Sub-GHz module service (TASKS.md STARK-0601,
 * ADR-0022 §2) over the real module core, a fake 24C02 for the descriptor and
 * the fake CC1101 for the chip: a descriptor alone never confirms the radio,
 * the chip's own answer does, and everything that can go wrong on the way ends
 * in a release, a log line and a retry, never a hang. The bridge's one job
 * (turning "ready" into the capability) is played by changed().
 */
#include <string.h>
#include "fake_24c02.h"
#include "fake_cc1101.h"
#include "fake_module_catalog.h"
#include "module_core.h"
#include "subghz_svc.h"
#include "unity.h"

#define A0 0x50
#define A1 0x51

static module_core_t s_mc;
static subghz_svc_t s_svc;
static uint32_t s_now;
static bool s_open;
static stark_err_t s_open_err;
static unsigned s_opens, s_closes, s_claims, s_releases, s_xfers, s_changes;
static unsigned s_fail_from; /* 1-based xfer after open from which every xfer fails */
static unsigned s_open_xfers;
static size_t s_list_extra; /* the list call reports this many more entries than it holds */

typedef struct {
    subghz_log_t what;
    unsigned a, b;
} logged_t;
static logged_t s_log[32];
static size_t s_nlog;

static stark_err_t mc_probe(void *ctx, uint8_t addr)
{
    (void)ctx;
    return fake_24c02_probe(addr);
}

static stark_err_t mc_read(void *ctx, uint8_t addr, uint8_t off, uint8_t *buf, size_t len)
{
    (void)ctx;
    return fake_24c02_read_at(addr, off, buf, len);
}

static const module_io_t k_mio = {mc_probe, mc_read, NULL, NULL, NULL};
static const module_wiring_t k_board[] = {{A0, 40, 41, 42}, {A1, -1, -1, -1}}; /* ADR-0021 §5 */

static size_t io_list(void *ctx, stark_module_entry_t *out, size_t max)
{
    (void)ctx;
    return module_core_list(&s_mc, out, max) + s_list_extra;
}

static stark_err_t io_claim(void *ctx, uint8_t slot, stark_module_lease_t *out)
{
    (void)ctx;
    s_claims++;
    return module_core_claim(&s_mc, slot, STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ, "subghz", out);
}

static void io_release(void *ctx, const stark_module_lease_t *l)
{
    (void)ctx;
    s_releases++;
    module_core_release(&s_mc, l);
}

static bool io_lease_valid(void *ctx, const stark_module_lease_t *l)
{
    (void)ctx;
    return module_core_lease_valid(&s_mc, l);
}

static stark_module_lease_t s_open_lease;

static stark_err_t io_open(void *ctx, const stark_module_lease_t *l)
{
    (void)ctx;
    s_opens++;
    if (s_open_err != STARK_OK) {
        return s_open_err;
    }
    int cs, i0, i1;
    stark_err_t err = module_core_lease_pins(&s_mc, l, &cs, &i0, &i1);
    if (err != STARK_OK) {
        return err;
    }
    TEST_ASSERT_EQUAL(40, cs); /* the lease carries slot 0's lines: no pin number in the service */
    TEST_ASSERT_EQUAL(41, i0);
    TEST_ASSERT_EQUAL(42, i1);
    s_open_lease = *l;
    s_open = true;
    s_open_xfers = 0;
    return STARK_OK;
}

static void io_close(void *ctx)
{
    (void)ctx;
    s_closes++;
    s_open = false;
}

static stark_err_t io_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    (void)ctx;
    s_xfers++;
    /* like production: a closed transport or a stale lease reaches nothing */
    if (!s_open || !module_core_lease_valid(&s_mc, &s_open_lease)) {
        return STARK_ERR_STATE;
    }
    if (s_fail_from != 0 && ++s_open_xfers >= s_fail_from) {
        return STARK_ERR_IO;
    }
    return fake_cc1101_xfer(tx, rx, len);
}

static uint32_t io_now(void *ctx)
{
    (void)ctx;
    return s_now;
}

static void io_delay(void *ctx, uint32_t ms)
{
    (void)ctx;
    s_now += ms;
}

static void io_log(void *ctx, subghz_log_t what, unsigned a, unsigned b)
{
    (void)ctx;
    if (s_nlog < sizeof s_log / sizeof s_log[0]) {
        s_log[s_nlog++] = (logged_t){what, a, b};
    }
}

static void io_changed(void *ctx, bool ready)
{
    (void)ctx;
    s_changes++;
    module_core_confirm(&s_mc, STARK_CAP_SUBGHZ, ready); /* main/module_bridge.c's job */
}

/* No receiver and no notify: this file never measures or captures. */
static const subghz_svc_io_t k_io = {
    io_list, io_claim, io_release, io_lease_valid, io_open,    io_close, io_xfer, NULL, NULL,
    NULL,    io_now,   io_delay,   io_log,         io_changed, NULL,     NULL,    NULL, NULL};

void setUp(void)
{
    fake_24c02_reset();
    fake_cc1101_reset();
    module_core_init(&s_mc, &k_mio, k_board, 2);
    subghz_svc_init(&s_svc, &k_io);
    s_now = 10000;
    s_open = false;
    s_open_err = STARK_OK;
    s_opens = s_closes = s_claims = s_releases = s_xfers = s_changes = 0;
    s_fail_from = 0;
    s_list_extra = 0;
    s_nlog = 0;
}
void tearDown(void)
{}

static void fit(uint8_t addr, int id)
{
    fake_24c02_put(addr, k_fake_modules[id - 1].image, MODULE_DESC_SIZE);
}

static void module_ticks(int n)
{
    for (int i = 0; i < n; i++) {
        (void)module_core_tick(&s_mc);
    }
}

static void fit_radio(void)
{
    fit(A0, FAKE_MODULE_CC1101_MODULE);
    module_ticks(2);
}

static void assert_log(size_t i, subghz_log_t what, unsigned a, unsigned b)
{
    TEST_ASSERT_TRUE_MESSAGE(i < s_nlog, "a log line is missing");
    TEST_ASSERT_EQUAL(what, s_log[i].what);
    TEST_ASSERT_EQUAL_HEX32(a, s_log[i].a);
    TEST_ASSERT_EQUAL_HEX32(b, s_log[i].b);
}

static void step_after(uint32_t ms)
{
    s_now += ms;
    subghz_svc_step(&s_svc);
}

static void test_no_module_says_so_once(void)
{
    subghz_svc_step(&s_svc);
    subghz_svc_step(&s_svc);
    TEST_ASSERT_EQUAL(1, s_nlog);
    assert_log(0, SUBGHZ_LOG_NO_MODULE, 0, 0);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL(0, s_claims);
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_mc));
}

static void test_other_modules_do_not_start_the_service(void)
{
    fit(A0, FAKE_MODULE_SPI_IRQ); /* wired for SPI and IRQ, but no radio */
    fit(A1, FAKE_MODULE_NFC_CARRIER);
    module_core_provide(&s_mc, STARK_MODULE_ONBOARD, "IR", STARK_CAP_IR_TX);
    module_ticks(2);
    subghz_svc_step(&s_svc);
    TEST_ASSERT_EQUAL(0, s_claims);
    assert_log(0, SUBGHZ_LOG_NO_MODULE, 0, 0);
}

static void test_a_descriptor_alone_never_sets_the_bit(void)
{
    fake_cc1101_present(false); /* declared, nothing behind it */
    fit_radio();
    stark_module_entry_t e[STARK_MODULE_ENTRIES];
    (void)module_core_list(&s_mc, e, STARK_MODULE_ENTRIES);
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, e[0].state);
    TEST_ASSERT_EQUAL_HEX32(0x38u, e[0].declared); /* SUBGHZ | MOD_SPI | MOD_IRQ */
    subghz_svc_step(&s_svc);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL(0, s_changes);
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_mc));
    (void)module_core_list(&s_mc, e, STARK_MODULE_ENTRIES);
    TEST_ASSERT_EQUAL_HEX32(0, e[0].confirmed);
    /* it claimed, found nothing that answers like a CC1101, and let go */
    TEST_ASSERT_EQUAL(1, s_claims);
    TEST_ASSERT_EQUAL(s_opens, s_closes);
    assert_log(0, SUBGHZ_LOG_NOT_CC1101, 0xFF, 0xFF);
    stark_module_lease_t other;
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_mc, 0, STARK_CAP_MOD_SPI, "other", &other));
}

static void test_the_chip_appearing_confirms_the_capability(void)
{
    fake_cc1101_present(false);
    fit_radio();
    subghz_svc_step(&s_svc);
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_mc));
    unsigned claims = s_claims;
    step_after(SUBGHZ_RETRY_MS - 1); /* too early: nothing is even tried */
    TEST_ASSERT_EQUAL(claims, s_claims);
    fake_cc1101_present(true);
    step_after(1);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL(1, s_changes);
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_SUBGHZ, module_core_caps(&s_mc));
    assert_log(1, SUBGHZ_LOG_VERSION, 0x14, 0);
    TEST_ASSERT_EQUAL(2, s_nlog);
    /* configured for the 433 MHz band, power capped, nothing sent */
    const fake_cc1101_stats_t *st = fake_cc1101_stats();
    TEST_ASSERT_EQUAL_HEX8(0x32, st->reg[CC1101_PKTCTRL0]);
    TEST_ASSERT_TRUE(st->tuned_hz > 433919000u && st->tuned_hz < 433921000u);
    TEST_ASSERT_EQUAL(0, st->out_of_band);
    TEST_ASSERT_EQUAL(0, st->tx_strobes);
    TEST_ASSERT_EQUAL_HEX8(0x60, st->patable_max);
    TEST_ASSERT_EQUAL(0, st->patable_other);
    TEST_ASSERT_EQUAL_HEX8(0x01, st->marcstate); /* idle until a later task listens */
}

static void test_a_chip_present_from_the_start(void)
{
    fit_radio();
    subghz_svc_step(&s_svc);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_SUBGHZ, module_core_caps(&s_mc));
    assert_log(0, SUBGHZ_LOG_VERSION, 0x14, 0);
}

static void test_the_other_known_version(void)
{
    fake_cc1101_chip(0x00, 0x04);
    fit_radio();
    subghz_svc_step(&s_svc);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    assert_log(0, SUBGHZ_LOG_VERSION, 0x04, 0);
}

static void test_a_wrong_chip_is_rejected_and_not_logged_twice(void)
{
    fake_cc1101_chip(0x01, 0x77);
    fit_radio();
    subghz_svc_step(&s_svc);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    assert_log(0, SUBGHZ_LOG_NOT_CC1101, 0x01, 0x77);
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_mc));
    TEST_ASSERT_EQUAL(0, s_changes);
    for (int i = 0; i < 4; i++) {
        step_after(SUBGHZ_RETRY_MAX_MS); /* tried again every period, said once */
    }
    TEST_ASSERT_EQUAL(5, s_claims);
    TEST_ASSERT_EQUAL(1, s_nlog);
    fake_cc1101_chip(0x02, 0x77); /* something else answers now: that is news */
    step_after(SUBGHZ_RETRY_MAX_MS);
    TEST_ASSERT_EQUAL(2, s_nlog);
    assert_log(1, SUBGHZ_LOG_NOT_CC1101, 0x02, 0x77);
    fake_cc1101_chip(0x00, 0x14);
    step_after(SUBGHZ_RETRY_MAX_MS);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
}

static void test_a_repeated_rejection_is_tried_less_and_less_often(void)
{
    fake_cc1101_chip(0x01, 0x77);
    fit_radio();
    subghz_svc_step(&s_svc);
    static const uint32_t waits[] = {2000, 4000, 8000, 8000, 8000};
    for (size_t i = 0; i < sizeof waits / sizeof waits[0]; i++) {
        unsigned claims = s_claims;
        step_after(waits[i] - 1);
        TEST_ASSERT_EQUAL_MESSAGE(claims, s_claims, "not before its time");
        step_after(1);
        TEST_ASSERT_EQUAL_MESSAGE(claims + 1, s_claims, "and then once");
    }
    TEST_ASSERT_EQUAL(1, s_nlog);
    fake_cc1101_chip(0x00, 0x14);
    step_after(SUBGHZ_RETRY_MAX_MS);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    fake_cc1101_chip(0x01, 0x77); /* a new rejection starts again at the short wait */
    subghz_svc_step(&s_svc);
    subghz_svc_step(&s_svc);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    unsigned claims = s_claims;
    step_after(SUBGHZ_RETRY_MS - 1);
    TEST_ASSERT_EQUAL(claims, s_claims);
    step_after(1);
    TEST_ASSERT_EQUAL(claims + 1, s_claims);
}

static void test_a_wrong_chip_swapped_in_while_ready(void)
{
    fit_radio();
    subghz_svc_step(&s_svc);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    fake_cc1101_chip(0x01, 0x14);
    subghz_svc_step(&s_svc); /* one bad answer is not yet a verdict */
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    subghz_svc_step(&s_svc);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_mc)); /* bit 5 withdrawn */
    assert_log(1, SUBGHZ_LOG_NOT_CC1101, 0x01, 0x14);
    TEST_ASSERT_EQUAL(2, s_changes);
    TEST_ASSERT_EQUAL(s_opens, s_closes);
    step_after(SUBGHZ_RETRY_MS); /* identified again: the same rejection is not repeated */
    TEST_ASSERT_EQUAL(2, s_nlog);
    fake_cc1101_chip(0x00, 0x14);
    step_after(SUBGHZ_RETRY_MAX_MS);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_SUBGHZ, module_core_caps(&s_mc));
}

static void test_one_bad_answer_does_not_drop_a_ready_module(void)
{
    fit_radio();
    subghz_svc_step(&s_svc);
    fake_cc1101_fault(FAKE_CC1101_BIT_FLIP, 1);
    subghz_svc_step(&s_svc);
    subghz_svc_step(&s_svc);
    subghz_svc_step(&s_svc);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL(1, s_nlog);
    TEST_ASSERT_EQUAL(1, s_changes);
}

static void test_removing_the_slot_withdraws_and_releases(void)
{
    fit_radio();
    subghz_svc_step(&s_svc);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    fake_24c02_remove(A0);
    module_ticks(2); /* the module core revokes the lease */
    unsigned xfers = s_xfers;
    subghz_svc_step(&s_svc);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_mc));
    assert_log(1, SUBGHZ_LOG_LOST, 0, 0);
    TEST_ASSERT_EQUAL(xfers, s_xfers); /* nothing reached the stale lease */
    TEST_ASSERT_EQUAL(s_opens, s_closes);
    TEST_ASSERT_FALSE(s_open);
    subghz_svc_step(&s_svc); /* no module again: nothing more to say */
    TEST_ASSERT_EQUAL(2, s_nlog);
    fit_radio(); /* and it comes back */
    subghz_svc_step(&s_svc);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_SUBGHZ, module_core_caps(&s_mc));
}

static void test_a_replaced_module_is_claimed_afresh(void)
{
    fit_radio();
    subghz_svc_step(&s_svc);
    fit(A0, FAKE_MODULE_NO_CAPS);
    module_ticks(2);
    subghz_svc_step(&s_svc);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    assert_log(1, SUBGHZ_LOG_LOST, 0, 0);
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_mc));
}

static void test_spi_errors_drop_a_ready_module_and_it_recovers(void)
{
    fit_radio();
    subghz_svc_step(&s_svc);
    fake_cc1101_fault(FAKE_CC1101_SPI_ERROR, -1);
    subghz_svc_step(&s_svc);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    subghz_svc_step(&s_svc);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    assert_log(1, SUBGHZ_LOG_LOST, 0, 0);
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_mc));
    step_after(SUBGHZ_RETRY_MS); /* still failing: a rejection, said once */
    step_after(SUBGHZ_RETRY_MS);
    TEST_ASSERT_EQUAL(3, s_nlog);
    assert_log(2, SUBGHZ_LOG_ERROR, (unsigned)STARK_ERR_IO, 0);
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);
    step_after(SUBGHZ_RETRY_MAX_MS);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_SUBGHZ, module_core_caps(&s_mc));
}

static void test_open_failure_releases_and_retries(void)
{
    fit_radio();
    s_open_err = STARK_ERR_IO;
    subghz_svc_step(&s_svc);
    TEST_ASSERT_EQUAL(1, s_nlog);
    assert_log(0, SUBGHZ_LOG_ERROR, (unsigned)STARK_ERR_IO, 0);
    TEST_ASSERT_EQUAL(1, s_releases);
    stark_module_lease_t other;
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_mc, 0, STARK_CAP_MOD_SPI, "other", &other));
    module_core_release(&s_mc, &other);
    s_open_err = STARK_OK;
    step_after(SUBGHZ_RETRY_MS - 1);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    step_after(1);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
}

static void test_a_slot_held_by_someone_else_is_retried_quietly(void)
{
    fit_radio();
    stark_module_lease_t other;
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_mc, 0, STARK_CAP_MOD_SPI, "other", &other));
    subghz_svc_step(&s_svc);
    subghz_svc_step(&s_svc);
    TEST_ASSERT_EQUAL(2, s_claims);
    TEST_ASSERT_EQUAL(0, s_nlog);
    TEST_ASSERT_EQUAL(0, s_opens);
    module_core_release(&s_mc, &other);
    subghz_svc_step(&s_svc);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
}

static void test_a_reset_that_never_completes_is_an_error_not_a_hang(void)
{
    fit_radio();
    uint8_t srx = 0x34;
    uint8_t rx;
    TEST_ASSERT_EQUAL(STARK_OK, fake_cc1101_xfer(&srx, &rx, 1)); /* RX, then wedged */
    fake_cc1101_fault(FAKE_CC1101_STUCK, -1);
    subghz_svc_step(&s_svc);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    assert_log(0, SUBGHZ_LOG_ERROR, (unsigned)STARK_ERR_TIMEOUT, 0);
    TEST_ASSERT_TRUE(s_now - 10000u <= 2u * CC1101_RESET_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(s_opens, s_closes);
    fake_cc1101_fault(FAKE_CC1101_NONE, 0);
    step_after(SUBGHZ_RETRY_MS);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
}

static void test_a_configuration_that_fails_is_an_error(void)
{
    fit_radio();
    s_fail_from = 5; /* reset takes two transfers and detection two: SIDLE is the fifth */
    subghz_svc_step(&s_svc);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    assert_log(0, SUBGHZ_LOG_ERROR, (unsigned)STARK_ERR_IO, 0);
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_mc));
    s_fail_from = 0;
    step_after(SUBGHZ_RETRY_MS);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
}

static void test_a_failure_is_logged_again_after_the_module_left_and_came_back(void)
{
    fake_cc1101_chip(0x01, 0x77);
    fit_radio();
    subghz_svc_step(&s_svc);
    fake_24c02_remove(A0);
    module_ticks(2);
    subghz_svc_step(&s_svc); /* nothing declares a radio any more: forgets the rejection */
    fit_radio();
    step_after(SUBGHZ_RETRY_MAX_MS);
    TEST_ASSERT_EQUAL(2, s_nlog);
    assert_log(0, SUBGHZ_LOG_NOT_CC1101, 0x01, 0x77);
    assert_log(1, SUBGHZ_LOG_NOT_CC1101, 0x01, 0x77); /* "no module" is said only at the start */
}

static void test_two_radios_conflict_and_neither_is_used(void)
{
    fit(A0, FAKE_MODULE_CC1101_MODULE);
    fit(A1, FAKE_MODULE_CC1101_MODULE);
    module_ticks(2);
    subghz_svc_step(&s_svc);
    TEST_ASSERT_FALSE(subghz_svc_ready(&s_svc));
    TEST_ASSERT_EQUAL(0, s_claims);
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_mc));
}

static void test_a_list_that_over_reports_is_clamped(void)
{
    fit_radio();
    s_list_extra = 100;
    subghz_svc_step(&s_svc);
    TEST_ASSERT_TRUE(subghz_svc_ready(&s_svc));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_module_says_so_once);
    RUN_TEST(test_other_modules_do_not_start_the_service);
    RUN_TEST(test_a_descriptor_alone_never_sets_the_bit);
    RUN_TEST(test_the_chip_appearing_confirms_the_capability);
    RUN_TEST(test_a_chip_present_from_the_start);
    RUN_TEST(test_the_other_known_version);
    RUN_TEST(test_a_wrong_chip_is_rejected_and_not_logged_twice);
    RUN_TEST(test_a_repeated_rejection_is_tried_less_and_less_often);
    RUN_TEST(test_a_wrong_chip_swapped_in_while_ready);
    RUN_TEST(test_one_bad_answer_does_not_drop_a_ready_module);
    RUN_TEST(test_removing_the_slot_withdraws_and_releases);
    RUN_TEST(test_a_replaced_module_is_claimed_afresh);
    RUN_TEST(test_spi_errors_drop_a_ready_module_and_it_recovers);
    RUN_TEST(test_open_failure_releases_and_retries);
    RUN_TEST(test_a_slot_held_by_someone_else_is_retried_quietly);
    RUN_TEST(test_a_reset_that_never_completes_is_an_error_not_a_hang);
    RUN_TEST(test_a_configuration_that_fails_is_an_error);
    RUN_TEST(test_a_failure_is_logged_again_after_the_module_left_and_came_back);
    RUN_TEST(test_two_radios_conflict_and_neither_is_used);
    RUN_TEST(test_a_list_that_over_reports_is_clamped);
    return UNITY_END();
}
