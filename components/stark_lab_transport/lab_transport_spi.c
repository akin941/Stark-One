/*
 * lab_transport_spi.c — the lab transport's SPI group on stark_module's lease and SPI device
 * (stark_lab_transport.h, STARK-0706). Target-only: the host tests reach the same guard through the
 * shim in test/host/support/lab_transport_spi_host.c, the emulator test image through its own
 * transport.
 *
 * It calls no SPI driver of its own (stark_module_spi.c is the only file that does, ARCHITECTURE
 * §8) and names no pin: the chip select is the lease's, so the panel's and the card's devices on
 * the same bus cannot be addressed. The guard runs before anything is attached.
 */
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
    stark_module_spi_t *dev = NULL;
    stark_err_t err = stark_module_spi_attach(l, hz, mode, &dev);
    if (err != STARK_OK) {
        return err;
    }
    err = stark_module_spi_xfer(dev, tx, rx, n);
    stark_module_spi_detach(dev);
    return err;
}
