/*
 * test_module_core.c — production discovery over the test-only fake 24C02
 * (TASKS.md STARK-0502, ADR-0021): insertion, removal, replacement, compatible
 * and conflicting modules, every corrupt vector, bus faults, recovery, the lock
 * never held across a bus call, and random ticks holding the invariants.
 */
#include <string.h>
#include "fake_24c02.h"
#include "fake_module_catalog.h"
#include "module_core.h"
#include "unity.h"

#define A0 0x50
#define A1 0x51

static module_core_t s_c;
static int s_locked;
static unsigned s_calls;

static stark_err_t io_probe(void *ctx, uint8_t addr)
{
    (void)ctx;
    TEST_ASSERT_EQUAL(0, s_locked); /* never across a bus call */
    s_calls++;
    return fake_24c02_probe(addr);
}

static stark_err_t io_read(void *ctx, uint8_t addr, uint8_t off, uint8_t *buf, size_t len)
{
    (void)ctx;
    TEST_ASSERT_EQUAL(0, s_locked);
    s_calls++;
    return fake_24c02_read_at(addr, off, buf, len);
}

static void io_lock(void *ctx)
{
    (void)ctx;
    TEST_ASSERT_EQUAL(0, s_locked);
    s_locked++;
}

static void io_unlock(void *ctx)
{
    (void)ctx;
    TEST_ASSERT_EQUAL(1, s_locked);
    s_locked--;
}

static const module_io_t k_io = {io_probe, io_read, io_lock, io_unlock, NULL};

void setUp(void)
{
    static const module_wiring_t wires[] = {{A0, 40, 41, 42}, {A1, 38, 39, 3}};
    fake_24c02_reset();
    module_core_init(&s_c, &k_io, wires, 2);
    s_calls = 0;
}
void tearDown(void)
{}

static void fit(uint8_t addr, int catalog_id)
{
    fake_24c02_put(addr, k_fake_modules[catalog_id - 1].image, MODULE_DESC_SIZE);
}

static stark_module_entry_t entry(size_t i)
{
    stark_module_entry_t list[STARK_MODULE_SLOTS];
    TEST_ASSERT_EQUAL(2, module_core_list(&s_c, list, 2));
    return list[i];
}

static void ticks(int n)
{
    for (int i = 0; i < n; i++) {
        (void)module_core_tick(&s_c);
    }
}

static void test_absent(void)
{
    TEST_ASSERT_EQUAL(0, module_core_tick(&s_c));
    TEST_ASSERT_EQUAL(0, module_core_tick(&s_c));
    TEST_ASSERT_EQUAL(STARK_MODULE_ABSENT, entry(0).state);
    TEST_ASSERT_EQUAL(STARK_MODULE_SLOT, entry(1).kind);
    TEST_ASSERT_EQUAL(1, entry(1).slot);
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_c));
    TEST_ASSERT_EQUAL(4, s_calls); /* a probe per slot per tick, no reads */
}

static void test_insert_needs_two_reads(void)
{
    fit(A0, FAKE_MODULE_NFC_CARRIER);
    TEST_ASSERT_EQUAL(0, module_core_tick(&s_c));
    TEST_ASSERT_EQUAL(STARK_MODULE_ABSENT, entry(0).state);
    TEST_ASSERT_EQUAL_HEX32(1, module_core_tick(&s_c));
    stark_module_entry_t e = entry(0);
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, e.state);
    TEST_ASSERT_EQUAL_HEX16(0x0101, e.id);
    TEST_ASSERT_EQUAL(2, e.rev);
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_NFC, e.declared);
    TEST_ASSERT_EQUAL_STRING("NFC carrier", e.name);
    TEST_ASSERT_EQUAL(0, module_core_tick(&s_c)); /* stable: no more changes */
}

static void test_declared_is_not_effective(void)
{
    fit(A0, FAKE_MODULE_NFC_CARRIER);
    ticks(2);
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_NFC, entry(0).declared);
    TEST_ASSERT_EQUAL_HEX32(0, entry(0).confirmed);
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_c));
    TEST_ASSERT_TRUE(module_core_confirm(&s_c, STARK_CAP_NFC, true));
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_NFC, module_core_caps(&s_c));
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_NFC, entry(0).confirmed);
    TEST_ASSERT_FALSE(module_core_confirm(&s_c, STARK_CAP_NFC, true)); /* no change */
    TEST_ASSERT_TRUE(module_core_confirm(&s_c, STARK_CAP_NFC, false));
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_c));
}

static void test_confirm_without_any_module(void)
{
    TEST_ASSERT_TRUE(module_core_confirm(&s_c, STARK_CAP_IR_TX | STARK_CAP_IR_RX, true));
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_IR_TX | STARK_CAP_IR_RX, module_core_caps(&s_c));
}

static void test_wiring_and_unknown_bits_never_effective(void)
{
    TEST_ASSERT_FALSE(module_core_confirm(&s_c, STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ, true));
    TEST_ASSERT_FALSE(module_core_confirm(&s_c, 0x80000020u, true));
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_c));
    fit(A0, FAKE_MODULE_UNKNOWN_BITS);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, entry(0).state);
    TEST_ASSERT_EQUAL_HEX32(0x80000020u | STARK_CAP_MOD_IRQ, entry(0).declared);
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_c));
}

static void test_removal_debounced(void)
{
    fit(A0, FAKE_MODULE_SPI_IRQ);
    ticks(2);
    fake_24c02_remove(A0);
    TEST_ASSERT_EQUAL(0, module_core_tick(&s_c)); /* one miss: nothing */
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, entry(0).state);
    TEST_ASSERT_EQUAL_HEX32(1, module_core_tick(&s_c));
    TEST_ASSERT_EQUAL(STARK_MODULE_ABSENT, entry(0).state);
    TEST_ASSERT_EQUAL_STRING("", entry(0).name);
    TEST_ASSERT_EQUAL(0, module_core_tick(&s_c));
    fit(A0, FAKE_MODULE_SPI_IRQ); /* the same one again is announced again */
    ticks(1);
    TEST_ASSERT_EQUAL_HEX32(1, module_core_tick(&s_c));
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, entry(0).state);
}

static void test_replacement(void)
{
    fit(A0, FAKE_MODULE_SPI_IRQ);
    ticks(2);
    fit(A0, FAKE_MODULE_NO_CAPS);
    TEST_ASSERT_EQUAL(0, module_core_tick(&s_c));
    TEST_ASSERT_EQUAL_STRING("SPI lab module", entry(0).name);
    TEST_ASSERT_EQUAL_HEX32(1, module_core_tick(&s_c));
    TEST_ASSERT_EQUAL_STRING("Passive", entry(0).name);
}

static void test_every_catalogue_entry(void)
{
    for (int id = 1; id <= FAKE_MODULE_COUNT; id++) {
        const fake_module_entry_t *m = &k_fake_modules[id - 1];
        setUp();
        fit(A0, id);
        ticks(2);
        stark_module_entry_t e = entry(0);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, module_core_caps(&s_c), m->label);
        if (m->result == MODULE_DESC_OK && m->supported) {
            TEST_ASSERT_EQUAL_MESSAGE(STARK_MODULE_KNOWN, e.state, m->label);
        } else {
            TEST_ASSERT_EQUAL_MESSAGE(STARK_MODULE_GENERIC, e.state, m->label);
            TEST_ASSERT_EQUAL_MESSAGE(m->result, e.why, m->label);
            TEST_ASSERT_EQUAL_MESSAGE(m->result == MODULE_DESC_OK, e.unsupported, m->label);
        }
        /* a corrupt or impossible descriptor is never effective, even if a
         * driver has confirmed the bit it names */
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, e.confirmed, m->label);
    }
}

static void test_compatible_modules(void)
{
    fit(A0, FAKE_MODULE_SPI_IRQ);
    fit(A1, FAKE_MODULE_SPI_IRQ_B);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, entry(0).state);
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, entry(1).state);
    fit(A1, FAKE_MODULE_SPI_IRQ); /* duplicate ID, no exclusive capability: compatible */
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, entry(0).state);
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, entry(1).state);
}

static void test_conflict_withholds_and_clears(void)
{
    module_core_confirm(&s_c, STARK_CAP_NFC | STARK_CAP_IR_TX, true);
    fit(A0, FAKE_MODULE_NFC_CARRIER);
    fit(A1, FAKE_MODULE_NFC_RIVAL);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_CONFLICT, entry(0).state);
    TEST_ASSERT_EQUAL(STARK_MODULE_CONFLICT, entry(1).state);
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_IR_TX, module_core_caps(&s_c)); /* unrelated bits stay */
    fake_24c02_remove(A1);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, entry(0).state);
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_NFC | STARK_CAP_IR_TX, module_core_caps(&s_c));
}

static void test_duplicate_id_with_exclusive_cap(void)
{
    module_core_confirm(&s_c, STARK_CAP_NFC, true);
    fit(A0, FAKE_MODULE_NFC_CARRIER);
    fit(A1, FAKE_MODULE_NFC_CARRIER);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_CONFLICT, entry(1).state);
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_c));
}

static void test_transient_errors_change_nothing(void)
{
    fit(A0, FAKE_MODULE_SPI_IRQ);
    ticks(2);
    static const fake_24c02_fault_t faults[] = {FAKE_24C02_NACK_ADDR, FAKE_24C02_TIMEOUT,
                                                FAKE_24C02_BUS_ERROR, FAKE_24C02_NACK_DATA,
                                                FAKE_24C02_TRUNCATE,  FAKE_24C02_BIT_FLIP};
    for (size_t f = 0; f < sizeof faults / sizeof faults[0]; f++) {
        fake_24c02_fault(faults[f], 1);
        /* slot 0's probe or read suffers it */
        TEST_ASSERT_EQUAL_MESSAGE(0, module_core_tick(&s_c) & 1u, "transient changed the entry");
        TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, entry(0).state);
        ticks(2);
        TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, entry(0).state);
    }
}

static void test_persistent_faults_and_recovery(void)
{
    fit(A0, FAKE_MODULE_SPI_IRQ);
    fake_24c02_fault(FAKE_24C02_NACK_DATA, -1); /* new slot: answers, unreadable */
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_ABSENT, entry(0).state);
    ticks(1);
    TEST_ASSERT_EQUAL(STARK_MODULE_GENERIC, entry(0).state);
    TEST_ASSERT_EQUAL(MODULE_DESC_TRUNCATED, entry(0).why);
    ticks(5); /* polling continues; bounded work */
    fake_24c02_fault(FAKE_24C02_NONE, 0);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, entry(0).state); /* recovered */

    fake_24c02_fault(FAKE_24C02_TIMEOUT, -1);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_ABSENT, entry(0).state); /* a dead bus looks like removal */
    fake_24c02_fault(FAKE_24C02_NONE, 0);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, entry(0).state);

    fake_24c02_fault(FAKE_24C02_NACK_DATA, -1); /* a known entry survives read errors */
    ticks(10);
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, entry(0).state);
    fake_24c02_fault(FAKE_24C02_NONE, 0);
}

static void test_persistent_corruption_is_accepted_as_generic(void)
{
    fit(A0, FAKE_MODULE_SPI_IRQ);
    ticks(2);
    fit(A0, FAKE_MODULE_BAD_CRC);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_GENERIC, entry(0).state);
    TEST_ASSERT_EQUAL(MODULE_DESC_BAD_CRC, entry(0).why);
    TEST_ASSERT_EQUAL_STRING("", entry(0).name);
}

static void test_removal_during_read(void)
{
    fit(A0, FAKE_MODULE_SPI_IRQ);
    ticks(2);
    fake_24c02_fault(FAKE_24C02_TRUNCATE, 1);
    ticks(1);
    fake_24c02_remove(A0);
    ticks(2);
    TEST_ASSERT_EQUAL(STARK_MODULE_ABSENT, entry(0).state);
}

static void test_list_limits(void)
{
    stark_module_entry_t one;
    TEST_ASSERT_EQUAL(2, module_core_list(&s_c, &one, 1));
    TEST_ASSERT_EQUAL(2, module_core_list(&s_c, NULL, 0));
}

/* STARK-0504: IR and the PN532 have no EEPROM; they read as present while a
 * driver confirms them. A slot's declaration confirms nothing. */
static void test_providers(void)
{
    module_core_provide(&s_c, STARK_MODULE_ONBOARD, "IR", STARK_CAP_IR_TX | STARK_CAP_IR_RX);
    module_core_provide(&s_c, STARK_MODULE_BUS, "NFC reader", STARK_CAP_NFC);
    module_core_provide(&s_c, STARK_MODULE_BUS, "ignored", STARK_CAP_NFC); /* the table is full */
    stark_module_entry_t list[STARK_MODULE_ENTRIES];
    TEST_ASSERT_EQUAL(4, module_core_list(&s_c, list, 4));
    TEST_ASSERT_EQUAL(STARK_MODULE_ONBOARD, list[2].kind);
    TEST_ASSERT_EQUAL(STARK_MODULE_ABSENT, list[2].state);
    TEST_ASSERT_EQUAL_STRING("NFC reader", list[3].name);

    fit(A0, FAKE_MODULE_NFC_CARRIER); /* declares NFC; the reader does not answer */
    ticks(2);
    (void)module_core_list(&s_c, list, 4);
    TEST_ASSERT_EQUAL(STARK_MODULE_ABSENT, list[3].state);
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_c));

    module_core_confirm(&s_c, STARK_CAP_NFC, true); /* it answers */
    (void)module_core_list(&s_c, list, 4);
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, list[3].state);
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_NFC, list[0].confirmed);
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_NFC, module_core_caps(&s_c));

    module_core_confirm(&s_c, STARK_CAP_NFC, false); /* lost during background work */
    TEST_ASSERT_EQUAL_HEX32(0, module_core_caps(&s_c));
    module_core_confirm(&s_c, STARK_CAP_IR_TX | STARK_CAP_IR_RX, true);
    (void)module_core_list(&s_c, list, 4);
    TEST_ASSERT_EQUAL(STARK_MODULE_KNOWN, list[2].state);
    TEST_ASSERT_EQUAL(STARK_MODULE_ABSENT, list[3].state);
    module_core_confirm(&s_c, STARK_CAP_NFC, true); /* restored */
    TEST_ASSERT_EQUAL_HEX32(STARK_CAP_NFC | STARK_CAP_IR_TX | STARK_CAP_IR_RX,
                            module_core_caps(&s_c));
    stark_module_entry_t two[2];
    TEST_ASSERT_EQUAL(4, module_core_list(&s_c, two, 2)); /* a short list stops at the slots */
}

static uint32_t s_seed = 0x0DDBA11u;
static uint32_t rnd(void)
{
    s_seed ^= s_seed << 13;
    s_seed ^= s_seed >> 17;
    s_seed ^= s_seed << 5;
    return s_seed;
}

static void test_random_ticks_hold_invariants(void)
{
    for (int i = 0; i < 10000; i++) {
        uint8_t addr = (rnd() & 1u) != 0 ? A0 : A1;
        switch (rnd() % 6) {
            case 0:
                fake_24c02_remove(addr);
                break;
            case 1:
                fit(addr, (int)(rnd() % FAKE_MODULE_COUNT) + 1);
                break;
            case 2:
                fake_24c02_fault((fake_24c02_fault_t)(rnd() % FAKE_24C02_FAULT_COUNT),
                                 (int)(rnd() % 3));
                break;
            case 3:
                (void)module_core_confirm(&s_c, rnd(), (rnd() & 1u) != 0);
                break;
            default:
                break;
        }
        s_calls = 0;
        (void)module_core_tick(&s_c);
        TEST_ASSERT_TRUE(s_calls <= 2u * STARK_MODULE_SLOTS); /* bounded work */
        uint32_t caps = module_core_caps(&s_c);
        TEST_ASSERT_EQUAL_HEX32(0, caps & ~STARK_CAPS_KNOWN);
        TEST_ASSERT_EQUAL_HEX32(0, caps & (STARK_CAP_MOD_SPI | STARK_CAP_MOD_IRQ));
        for (size_t s = 0; s < 2; s++) {
            stark_module_entry_t e = entry(s);
            TEST_ASSERT_TRUE(strlen(e.name) <= MODULE_NAME_MAX);
            if (e.state == STARK_MODULE_ABSENT) {
                TEST_ASSERT_EQUAL_HEX32(0, e.declared);
            }
        }
    }
    TEST_ASSERT_EQUAL(0, s_locked);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_absent);
    RUN_TEST(test_insert_needs_two_reads);
    RUN_TEST(test_declared_is_not_effective);
    RUN_TEST(test_confirm_without_any_module);
    RUN_TEST(test_wiring_and_unknown_bits_never_effective);
    RUN_TEST(test_removal_debounced);
    RUN_TEST(test_replacement);
    RUN_TEST(test_every_catalogue_entry);
    RUN_TEST(test_compatible_modules);
    RUN_TEST(test_conflict_withholds_and_clears);
    RUN_TEST(test_duplicate_id_with_exclusive_cap);
    RUN_TEST(test_transient_errors_change_nothing);
    RUN_TEST(test_persistent_faults_and_recovery);
    RUN_TEST(test_persistent_corruption_is_accepted_as_generic);
    RUN_TEST(test_removal_during_read);
    RUN_TEST(test_list_limits);
    RUN_TEST(test_providers);
    RUN_TEST(test_random_ticks_hold_invariants);
    return UNITY_END();
}
