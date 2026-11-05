/*
 * stark_module_spi.c — a claimed slot's device on the shared SPI2 bus
 * (stark_module.h, STARK-0503). The third legitimate SPI2 user (ARCHITECTURE
 * §8); ESP-IDF's spi_master bus lock keeps its transactions apart from the
 * panel's and the SD card's, and STARK adds no bus mutex (ADR-0018 §4).
 * Target-only.
 */
#include <string.h>
#include "driver/spi_master.h"
#include "stark_module.h"

struct stark_module_spi {
    stark_module_lease_t lease;
    spi_device_handle_t dev;
};

static stark_module_spi_t s_devs[STARK_MODULE_SLOTS]; /* no allocation of ours */

stark_err_t stark_module_spi_attach(const stark_module_lease_t *l, uint32_t hz, uint8_t mode,
                                    stark_module_spi_t **out)
{
    if (l == NULL || out == NULL || l->slot >= STARK_MODULE_SLOTS || mode > 3) {
        return STARK_ERR_INVALID_ARG;
    }
    int cs = -1;
    int irq0 = -1;
    int irq1 = -1;
    stark_err_t err = stark_module_lease_pins(l, &cs, &irq0, &irq1);
    if (err != STARK_OK) {
        return err;
    }
    stark_module_spi_t *d = &s_devs[l->slot];
    if (cs < 0) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (d->dev != NULL) {
        return STARK_ERR_BUSY;
    }
    const spi_device_interface_config_t cfg = {
        .clock_speed_hz = (int)hz,
        .mode = mode,
        .spics_io_num = cs,
        .queue_size = 1,
    };
    if (spi_bus_add_device(SPI2_HOST, &cfg, &d->dev) != ESP_OK) {
        d->dev = NULL;
        return STARK_ERR_IO;
    }
    d->lease = *l;
    *out = d;
    return STARK_OK;
}

stark_err_t stark_module_spi_xfer(stark_module_spi_t *d, const void *tx, void *rx, size_t len)
{
    if (d == NULL || d->dev == NULL || len == 0) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!stark_module_lease_valid(&d->lease)) {
        return STARK_ERR_STATE; /* the module is gone: nothing reaches the bus */
    }
    spi_transaction_t t;
    memset(&t, 0, sizeof t);
    t.length = len * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    return spi_device_transmit(d->dev, &t) == ESP_OK ? STARK_OK : STARK_ERR_IO;
}

void stark_module_spi_detach(stark_module_spi_t *d)
{
    if (d != NULL && d->dev != NULL) {
        (void)spi_bus_remove_device(d->dev);
        d->dev = NULL;
    }
}
