/*
 * test_module_claim.c — CS / IRQ arbitration (TASKS.md STARK-0503, ADR-0021 §5)
 * on an injected table with two wired slots, over the fake 24C02: independent
 * claims, chip selects never crossed, every refusal, and a lease that dies with
 * its module. Software only: real signals are V1 (ROADMAP V1 exit 11).
 */
#include <string.h>
#include "fake_24c02.h"
#include "fake_module_catalog.h"
#include "module_core.h"
#include "unity.h"

#define A0   0x50
#define A1   0x51
#define BOTH (STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ)

static module_core_t s_c;

static stark_err_t io_probe(void *ctx, uint8_t addr)
{
    (void)ctx;
    return fake_24c02_probe(addr);
}

static stark_err_t io_read(void *ctx, uint8_t addr, uint8_t off, uint8_t *buf, size_t len)
{
    (void)ctx;
    return fake_24c02_read_at(addr, off, buf, len);
}

static const module_io_t k_io = {io_probe, io_read, NULL, NULL, NULL};
static const module_wiring_t k_two[] = {{A0, 40, 41, 42}, {A1, 38, 39, 3}};
static const module_wiring_t k_board[] = {{A0, 40, 41, 42}, {A1, -1, -1, -1}}; /* ADR-0021 §5 */

void setUp(void)
{
    fake_24c02_reset();
    module_core_init(&s_c, &k_io, k_two, 2);
}
void tearDown(void)
{}

static void fit(uint8_t addr, int id)
{
    fake_24c02_put(addr, k_fake_modules[id - 1].image, MODULE_DESC_SIZE);
}

static void ticks(int n)
{
    for (int i = 0; i < n; i++) {
        (void)module_core_tick(&s_c);
    }
}

static void test_two_slots_never_crossed(void)
{
    fit(A0, FAKE_MODULE_SPI_IRQ);
    fit(A1, FAKE_MODULE_SPI_IRQ_B);
    ticks(2);
    stark_module_lease_t a;
    stark_module_lease_t b;
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_c, 0, BOTH, "a", &a));
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_c, 1, BOTH, "b", &b));
    int cs;
    int i0;
    int i1;
    TEST_ASSERT_EQUAL(STARK_OK, module_core_lease_pins(&s_c, &a, &cs, &i0, &i1));
    TEST_ASSERT_EQUAL(40, cs);
    TEST_ASSERT_EQUAL(41, i0);
    TEST_ASSERT_EQUAL(42, i1);
    TEST_ASSERT_EQUAL(STARK_OK, module_core_lease_pins(&s_c, &b, &cs, &i0, &i1));
    TEST_ASSERT_EQUAL(38, cs);
    TEST_ASSERT_EQUAL(39, i0);
    TEST_ASSERT_EQUAL(3, i1);
    fake_24c02_remove(A0); /* one module leaving does not touch the other's lease */
    ticks(2);
    TEST_ASSERT_FALSE(module_core_lease_valid(&s_c, &a));
    TEST_ASSERT_TRUE(module_core_lease_valid(&s_c, &b));
}

static void test_only_the_claimed_lines(void)
{
    fit(A0, FAKE_MODULE_SPI_IRQ);
    ticks(2);
    stark_module_lease_t l;
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_c, 0, STARK_CAP_MOD_SPI, "a", &l));
    int cs;
    int i0;
    int i1;
    TEST_ASSERT_EQUAL(STARK_OK, module_core_lease_pins(&s_c, &l, &cs, &i0, &i1));
    TEST_ASSERT_EQUAL(40, cs);
    TEST_ASSERT_EQUAL(-1, i0);
    TEST_ASSERT_EQUAL(-1, i1);
    module_core_release(&s_c, &l);
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_c, 0, STARK_CAP_MOD_IRQ, "a", &l));
    TEST_ASSERT_EQUAL(STARK_OK, module_core_lease_pins(&s_c, &l, &cs, &i0, &i1));
    TEST_ASSERT_EQUAL(-1, cs);
    TEST_ASSERT_EQUAL(41, i0);
    TEST_ASSERT_EQUAL_INT(STARK_ERR_INVALID_ARG, module_core_lease_pins(&s_c, &l, NULL, &i0, &i1));
    TEST_ASSERT_EQUAL_INT(STARK_ERR_INVALID_ARG, module_core_lease_pins(&s_c, &l, &cs, NULL, &i1));
    TEST_ASSERT_EQUAL_INT(STARK_ERR_INVALID_ARG, module_core_lease_pins(&s_c, &l, &cs, &i0, NULL));
}

static void test_refusals(void)
{
    stark_module_lease_t l;
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, module_core_claim(&s_c, 0, BOTH, "a", &l)); /* empty */
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, module_core_claim(&s_c, 2, BOTH, "a", &l));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, module_core_claim(&s_c, 0, BOTH, NULL, &l));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, module_core_claim(&s_c, 0, BOTH, "a", NULL));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, module_core_claim(&s_c, 0, 0, "a", &l));
    TEST_ASSERT_EQUAL(STARK_ERR_INVALID_ARG, module_core_claim(&s_c, 0, STARK_CAP_NFC, "a", &l));

    fit(A0, FAKE_MODULE_BAD_CRC); /* generic */
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, module_core_claim(&s_c, 0, BOTH, "a", &l));

    fit(A0, FAKE_MODULE_SPI_ONLY); /* declares SPI, not IRQ */
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED, module_core_claim(&s_c, 0, BOTH, "a", &l));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      module_core_claim(&s_c, 0, STARK_CAP_MOD_IRQ, "a", &l));
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_c, 0, STARK_CAP_MOD_SPI, "a", &l));
    stark_module_lease_t other;
    TEST_ASSERT_EQUAL(STARK_ERR_BUSY,
                      module_core_claim(&s_c, 0, STARK_CAP_MOD_SPI, "b", &other)); /* one owner */

    fit(A0, FAKE_MODULE_NFC_CARRIER); /* conflict */
    fit(A1, FAKE_MODULE_NFC_RIVAL);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_FOUND, module_core_claim(&s_c, 1, STARK_CAP_MOD_IRQ, "a", &l));
}

static void test_unwired_slot_refused_on_the_real_table(void)
{
    module_core_init(&s_c, &k_io, k_board, 2);
    fit(A1, FAKE_MODULE_SPI_IRQ);
    ticks(2);
    stark_module_lease_t l;
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      module_core_claim(&s_c, 1, STARK_CAP_MOD_SPI, "a", &l));
    TEST_ASSERT_EQUAL(STARK_ERR_NOT_SUPPORTED,
                      module_core_claim(&s_c, 1, STARK_CAP_MOD_IRQ, "a", &l));
}

static void test_stale_after_removal_replacement_release(void)
{
    fit(A0, FAKE_MODULE_SPI_IRQ);
    ticks(2);
    stark_module_lease_t l;
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_c, 0, BOTH, "a", &l));
    TEST_ASSERT_TRUE(module_core_lease_valid(&s_c, &l));

    fake_24c02_remove(A0); /* one miss: still valid (debounced) */
    ticks(1);
    TEST_ASSERT_TRUE(module_core_lease_valid(&s_c, &l));
    ticks(1);
    TEST_ASSERT_FALSE(module_core_lease_valid(&s_c, &l));
    int cs;
    int i0;
    int i1;
    TEST_ASSERT_EQUAL(STARK_ERR_STATE, module_core_lease_pins(&s_c, &l, &cs, &i0, &i1));
    module_core_release(&s_c, &l); /* a stale release is harmless */

    fit(A0, FAKE_MODULE_SPI_IRQ); /* the same module back: the old lease stays dead */
    ticks(2);
    TEST_ASSERT_FALSE(module_core_lease_valid(&s_c, &l));
    stark_module_lease_t l2;
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_c, 0, BOTH, "a", &l2));
    module_core_release(&s_c, &l); /* a stale release does not free the new owner */
    TEST_ASSERT_TRUE(module_core_lease_valid(&s_c, &l2));

    fit(A0, FAKE_MODULE_SPI_IRQ_B); /* replaced */
    ticks(2);
    TEST_ASSERT_FALSE(module_core_lease_valid(&s_c, &l2));
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_c, 0, BOTH, "a", &l2));
    module_core_release(&s_c, &l2);
    TEST_ASSERT_FALSE(module_core_lease_valid(&s_c, &l2));
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_c, 0, BOTH, "b", &l2)); /* free again */
    TEST_ASSERT_FALSE(module_core_lease_valid(&s_c, NULL));
    stark_module_lease_t bad = {.slot = 9, .gen = 0};
    TEST_ASSERT_FALSE(module_core_lease_valid(&s_c, &bad));
}

static void test_conflict_revokes(void)
{
    fit(A0, FAKE_MODULE_ALL_KNOWN_SLOT); /* NFC + IRQ */
    ticks(2);
    stark_module_lease_t l;
    TEST_ASSERT_EQUAL(STARK_OK, module_core_claim(&s_c, 0, STARK_CAP_MOD_IRQ, "a", &l));
    fit(A1, FAKE_MODULE_NFC_RIVAL);
    ticks(2);
    TEST_ASSERT_FALSE(module_core_lease_valid(&s_c, &l));
}

static uint32_t s_seed = 0xC1A1C1A1u;
static uint32_t rnd(void)
{
    s_seed ^= s_seed << 13;
    s_seed ^= s_seed >> 17;
    s_seed ^= s_seed << 5;
    return s_seed;
}

/* At most one live lease per slot, and a live lease always names a known module. */
static void test_random_sequences(void)
{
    stark_module_lease_t held[2] = {{0}, {0}};
    bool has[2] = {false, false};
    for (int i = 0; i < 10000; i++) {
        uint8_t slot = (uint8_t)(rnd() & 1u);
        uint8_t addr = (uint8_t)(A0 + slot);
        switch (rnd() % 5) {
            case 0:
                fit(addr, (int)(rnd() % FAKE_MODULE_COUNT) + 1);
                break;
            case 1:
                fake_24c02_remove(addr);
                break;
            case 2: {
                stark_module_lease_t l;
                stark_err_t e = module_core_claim(&s_c, slot, STARK_CAP_MOD_IRQ, "t", &l);
                if (e == STARK_OK) {
                    TEST_ASSERT_FALSE(has[slot] && module_core_lease_valid(&s_c, &held[slot]));
                    held[slot] = l;
                    has[slot] = true;
                }
                break;
            }
            case 3:
                if (has[slot]) {
                    module_core_release(&s_c, &held[slot]);
                }
                break;
            default:
                break;
        }
        (void)module_core_tick(&s_c);
        stark_module_entry_t list[2];
        (void)module_core_list(&s_c, list, 2);
        for (int s = 0; s < 2; s++) {
            if (has[s] && module_core_lease_valid(&s_c, &held[s])) {
                TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, list[s].state);
                TEST_ASSERT_NOT_EQUAL(0, list[s].declared & STARK_CAP_MOD_IRQ);
            }
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_two_slots_never_crossed);
    RUN_TEST(test_only_the_claimed_lines);
    RUN_TEST(test_refusals);
    RUN_TEST(test_unwired_slot_refused_on_the_real_table);
    RUN_TEST(test_stale_after_removal_replacement_release);
    RUN_TEST(test_conflict_revokes);
    RUN_TEST(test_random_sequences);
    return UNITY_END();
}
