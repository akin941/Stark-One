/*
 * lab_transport_spi_host.c — the lab transport's SPI group for the host tests (TASKS.md
 * STARK-0706): the production header's guard (stark_lab_spi_args_ok(), stark_lab_spi_slot_ok()) and
 * the lease check in front of the test-only SPI devices (test/fake/fake_lab.c). The production file
 * (components/stark_lab_transport/lab_transport_spi.c) needs stark_module's SPI device, which needs
 * the ESP-IDF driver, and is built only for the target; this one is the same code with the device
 * replaced, so what the guard refuses is refused on the host exactly as it is on the board. The
 * test supplies stark_module_list() and stark_module_lease_valid(). Host tests only.
 */
#include "fake_lab.h"
#include "stark_lab_transport.h"

stark_err_t stark_lab_transport_spi_xfer(const stark_module_lease_t *l, uint32_t hz, uint8_t mode,
                                         const uint8_t *tx, uint8_t *rx, size_t n)
{
    if (l == NULL || tx == NULL || rx == NULL || !stark_lab_spi_args_ok(hz, mode, n)) {
        return STARK_ERR_INVALID_ARG;
    }
    stark_module_entry_t list[STARK_MODULE_ENTRIES];
    size_t count = stark_module_list(list, STARK_MODULE_ENTRIES);
    if (count > STARK_MODULE_ENTRIES) {
        count = STARK_MODULE_ENTRIES;
    }
    if (!stark_lab_spi_slot_ok(list, count, (int)l->slot)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (!stark_module_lease_valid(l)) {
        return STARK_ERR_STATE;
    }
    return fake_lab_spi_xfer(l->slot, hz, mode, tx, rx, n);
}
