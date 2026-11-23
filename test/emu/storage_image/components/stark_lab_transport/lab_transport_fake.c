/*
 * lab_transport_fake.c — test substitute for components/stark_lab_transport (ADR-0023 §2): the
 * same API, answered by the test-only pad model (test/fake/fake_lab.c) instead of the GPIO
 * matrix. Test image only. Like production it applies the shared pin guard
 * (stark_lab_pad_ok(), stark_lab_pad_hiz_ok()), so it can never touch a pin the policy
 * refuses; the pad model itself knows no policy.
 *
 * The pokes (scripts/test_emu.py POKE_VARS) that drive the model's external sources and wires
 * arrive with the tasks that use them (STARK-0702 and later). Integration evidence only:
 * nothing here shows a level on a pad.
 */
#include "fake_lab.h"
#include "stark_lab_transport.h"

static bool s_open;

stark_err_t stark_lab_transport_open(void)
{
    if (!s_open) {
        fake_lab_reset();
        s_open = true;
    }
    return STARK_OK;
}

void stark_lab_transport_close(void)
{
    s_open = false;
}

stark_err_t stark_lab_transport_pad_in(int pin, uint8_t pull)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    return fake_lab_pad_in(pin, pull);
}

stark_err_t stark_lab_transport_pad_out(int pin, bool level)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    return fake_lab_pad_out(pin, level);
}

stark_err_t stark_lab_transport_pad_read(int pin, bool *level)
{
    if (!s_open) {
        return STARK_ERR_STATE;
    }
    if (!stark_lab_pad_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    return fake_lab_pad_read(pin, level);
}

stark_err_t stark_lab_transport_pad_hiz(int pin)
{
    if (!stark_lab_pad_hiz_ok(pin)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    return fake_lab_pad_hiz(pin);
}
