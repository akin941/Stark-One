/*
 * lab_transport_i2c.c — the lab transport's I2C group on ESP-IDF's I2C master driver
 * (stark_lab_transport.h). Target-only: the host tests reach the same guard through the shim in
 * test/host/support/lab_transport_i2c_host.c, the emulator test image through its own transport.
 *
 * Every call is checked by the shared guard before the bus is touched (stark_lab_i2c_args_ok(),
 * stark_lab_i2c_allowed()). The bus is the board's shared one (stark_board_i2c_bus()): a probe is
 * ESP-IDF's i2c_master_probe(), a transaction attaches a device handle for itself and removes it
 * again, so no handle outlives a call and the bus's own lock keeps it from interleaving with the
 * NFC and module tasks. The NFC and module drivers keep their own handles at the same addresses;
 * the driver allows that and applies each handle's own clock to its own transactions.
 */
#include "driver/i2c_master.h"
#include "stark_board.h"
#include "stark_lab_transport.h"

static stark_err_t map(esp_err_t err)
{
    switch (err) {
        case ESP_OK:
            return STARK_OK;
        case ESP_ERR_NOT_FOUND:        /* i2c_master_probe() */
        case ESP_ERR_INVALID_RESPONSE: /* a transaction's address NACK */
            return STARK_ERR_NOT_FOUND;
        case ESP_ERR_TIMEOUT:
            return STARK_ERR_TIMEOUT;
        default:
            return STARK_ERR_IO;
    }
}

stark_err_t stark_lab_transport_i2c_probe(uint8_t addr, uint32_t timeout_ms)
{
    if (!stark_lab_i2c_args_ok(addr, 0u, 0u, 0u, timeout_ms)) {
        return STARK_ERR_INVALID_ARG;
    }
    i2c_master_bus_handle_t bus = stark_board_i2c_bus();
    if (bus == NULL) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    return map(i2c_master_probe(bus, addr, (int)timeout_ms));
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
    i2c_master_bus_handle_t bus = stark_board_i2c_bus();
    if (bus == NULL) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = hz,
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &dev);
    if (err != ESP_OK) {
        return STARK_ERR_IO;
    }
    if (wn > 0u && rn > 0u) {
        err = i2c_master_transmit_receive(dev, w, wn, r, rn, (int)timeout_ms);
    } else if (wn > 0u) {
        err = i2c_master_transmit(dev, w, wn, (int)timeout_ms);
    } else {
        err = i2c_master_receive(dev, r, rn, (int)timeout_ms);
    }
    (void)i2c_master_bus_rm_device(dev);
    return map(err);
}
