/*
 * module_transport_fake.c — test substitute for
 * components/stark_module_transport (ADR-0021 §6): the same API, answered by
 * the test-only fake 24C02 (test/fake/fake_24c02.c). Test image only.
 *
 * Scenarios steer it through GDB pokes (scripts/test_emu.py POKE_VARS):
 * emu_mod_slot0 / emu_mod_slot1 fit catalogue entry n
 * (test/fake/fake_module_catalog.h, 1-based; 0 empties the slot); emu_mod_fault
 * selects a fake_24c02_fault_t, applied until it changes again (0 clears it).
 */
#include "fake_24c02.h"
#include "fake_module_catalog.h"
#include "stark_module_transport.h"

#define SLOTS 2

void module_traffic_start(void); /* module_traffic.c */

volatile uint8_t emu_mod_slot0;
volatile uint8_t emu_mod_slot1;
volatile uint8_t emu_mod_fault;

static uint8_t s_applied[SLOTS];
static uint8_t s_fault_applied;

static void apply_slot(unsigned slot, uint8_t want)
{
    if (want == s_applied[slot] || want > FAKE_MODULE_COUNT) {
        return;
    }
    uint8_t addr = (uint8_t)(0x50u + slot);
    if (want == 0) {
        fake_24c02_remove(addr);
    } else {
        fake_24c02_put(addr, k_fake_modules[want - 1].image, MODULE_DESC_SIZE);
    }
    s_applied[slot] = want;
}

/* Applies changed pokes before each transaction. */
static void follow_pokes(void)
{
    apply_slot(0, emu_mod_slot0);
    apply_slot(1, emu_mod_slot1);
    uint8_t fault = emu_mod_fault;
    if (fault != s_fault_applied && fault < FAKE_24C02_FAULT_COUNT) {
        fake_24c02_fault((fake_24c02_fault_t)fault, fault == 0 ? 0 : -1);
        s_fault_applied = fault;
    }
}

stark_err_t stark_module_transport_open(void)
{
    fake_24c02_reset();
    module_traffic_start();
    return STARK_OK;
}

stark_err_t stark_module_transport_probe(uint8_t addr, uint32_t timeout_ms)
{
    (void)timeout_ms;
    follow_pokes();
    return fake_24c02_probe(addr);
}

stark_err_t stark_module_transport_read(uint8_t addr, uint8_t off, uint8_t *buf, size_t len,
                                        uint32_t timeout_ms)
{
    (void)timeout_ms;
    follow_pokes();
    return fake_24c02_read_at(addr, off, buf, len);
}
