/*
 * nfc_transport_fake.c — test substitute for components/stark_nfc_transport
 * (ADR-0020 §3): the same API, answered by the test-only fake PN532
 * (test/fake/fake_pn532.c) instead of the I2C bus. Test image only.
 *
 * Scenarios steer it through GDB pokes (scripts/test_emu.py POKE_VARS):
 * emu_nfc_tag puts catalogue tag n (test/fake/fake_tag_catalog.h, 1-based;
 * 0 empties the field) in front of the antenna; emu_nfc_fault selects a
 * fake_fault_t, applied until it changes again (0 clears it).
 */
#include "fake_pn532.h"
#include "fake_tag_catalog.h"
#include "stark_nfc_transport.h"

volatile uint8_t emu_nfc_tag;
volatile uint8_t emu_nfc_fault;

static uint8_t s_fault_applied;
static uint8_t s_tag_applied;
static bool s_open;

/* Applies changed pokes before each transfer. */
static void follow_pokes(void)
{
    uint8_t fault = emu_nfc_fault;
    if (fault != s_fault_applied && fault < FAKE_FAULT_COUNT) {
        fake_pn532_fault((fake_fault_t)fault, -1);
        s_fault_applied = fault;
    }
    uint8_t tag = emu_nfc_tag;
    if (tag != s_tag_applied && tag <= FAKE_CATALOG_COUNT) {
        const fake_catalog_entry_t *e = tag > 0 ? &k_fake_catalog[tag - 1] : NULL;
        fake_pn532_tag(e != NULL ? &e->tag : NULL);
        if (e != NULL && e->vanish_after >= 0) {
            fake_pn532_vanish_after((unsigned)e->vanish_after);
        }
        s_tag_applied = tag;
    }
}

stark_err_t stark_nfc_transport_open(void)
{
    if (!s_open) {
        fake_pn532_reset();
        s_open = true;
    }
    return STARK_OK;
}

void stark_nfc_transport_close(void)
{
    s_open = false;
}

stark_err_t stark_nfc_transport_write(const uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    follow_pokes();
    return fake_pn532_write(buf, len);
}

stark_err_t stark_nfc_transport_read(uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    follow_pokes();
    return fake_pn532_read(buf, len);
}
