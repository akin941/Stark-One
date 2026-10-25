/*
 * nfc_transport_fake.c — test substitute for components/stark_nfc_transport
 * (ADR-0020 §3): the same API, answered by the test-only fake PN532
 * (test/fake/fake_pn532.c) instead of the I2C bus. Test image only.
 *
 * Scenarios steer it through a GDB poke (scripts/test_emu.py POKE_VARS):
 * emu_nfc_fault selects a fake_fault_t, applied until it changes again
 * (0 clears it).
 */
#include "fake_pn532.h"
#include "stark_nfc_transport.h"

volatile uint8_t emu_nfc_fault;

static uint8_t s_applied;
static bool s_open;

/* Applies a changed poke before each transfer. */
static void follow_pokes(void)
{
    uint8_t fault = emu_nfc_fault;
    if (fault != s_applied && fault < FAKE_FAULT_COUNT) {
        fake_pn532_fault((fake_fault_t)fault, -1);
        s_applied = fault;
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
