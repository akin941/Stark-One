/*
 * lab_transport_i2c_host.c — the lab transport's I2C group for the host tests (TASKS.md
 * STARK-0704): the production header's guard (stark_lab_i2c_args_ok(), stark_lab_i2c_allowed())
 * in front of the test-only bus (test/fake/fake_lab.c). The production file
 * (components/stark_lab_transport/lab_transport_i2c.c) needs ESP-IDF's I2C master driver and is
 * built only for the target; this one is the same code with the driver replaced, so what the
 * guard refuses is refused on the host exactly as it is on the board. The test's own
 * stark_board_pins() and stark_module_caps() decide the EEPROM addresses and whether NFC is
 * confirmed. Host tests only.
 */
#include "fake_lab.h"
#include "stark_lab_transport.h"

stark_err_t stark_lab_transport_i2c_probe(uint8_t addr, uint32_t timeout_ms)
{
    if (!stark_lab_i2c_args_ok(addr, 0u, 0u, 0u, timeout_ms)) {
        return STARK_ERR_INVALID_ARG;
    }
    return fake_lab_i2c_probe(addr);
}

stark_err_t stark_lab_transport_i2c_xfer(uint8_t addr, uint32_t hz, const uint8_t *w, size_t wn,
                                         uint8_t *r, size_t rn, uint32_t timeout_ms)
{
    if (!stark_lab_i2c_args_ok(addr, wn, rn, hz, timeout_ms) || (wn > 0u && w == NULL) ||
        (rn > 0u && r == NULL)) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!stark_lab_i2c_allowed(stark_board_pins(), (stark_module_caps() & STARK_CAP_NFC) != 0u,
                               addr, wn, rn)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    return fake_lab_i2c_xfer(addr, w, wn, r, rn);
}
