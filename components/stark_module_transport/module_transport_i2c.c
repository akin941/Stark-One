/*
 * module_transport_i2c.c — ID EEPROMs over ESP-IDF's I2C master driver
 * (stark_module_transport.h). Target-only.
 */
#include "driver/i2c_master.h"
#include "sdkconfig.h"
#include "stark_board.h"
#include "stark_module_transport.h"

#define MAX_DEVICES 2 /* one per slot, HARDWARE.md §6 */

static i2c_master_bus_handle_t s_bus;
static struct {
    uint8_t addr;
    i2c_master_dev_handle_t dev;
} s_devs[MAX_DEVICES];

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

/* Attached on first use: an empty slot costs no device handle. */
static i2c_master_dev_handle_t device(uint8_t addr)
{
    for (size_t i = 0; i < MAX_DEVICES; i++) {
        if (s_devs[i].dev != NULL && s_devs[i].addr == addr) {
            return s_devs[i].dev;
        }
    }
    for (size_t i = 0; i < MAX_DEVICES; i++) {
        if (s_devs[i].dev == NULL) {
            const i2c_device_config_t cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = addr,
                .scl_speed_hz = CONFIG_STARK_MODULE_I2C_HZ,
            };
            if (i2c_master_bus_add_device(s_bus, &cfg, &s_devs[i].dev) != ESP_OK) {
                s_devs[i].dev = NULL;
                return NULL;
            }
            s_devs[i].addr = addr;
            return s_devs[i].dev;
        }
    }
    return NULL;
}

stark_err_t stark_module_transport_open(void)
{
    s_bus = stark_board_i2c_bus();
    return s_bus != NULL ? STARK_OK : STARK_ERR_NOT_SUPPORTED;
}

stark_err_t stark_module_transport_probe(uint8_t addr, uint32_t timeout_ms)
{
    if (s_bus == NULL) {
        return STARK_ERR_STATE;
    }
    return map(i2c_master_probe(s_bus, addr, (int)timeout_ms));
}

stark_err_t stark_module_transport_read(uint8_t addr, uint8_t off, uint8_t *buf, size_t len,
                                        uint32_t timeout_ms)
{
    if (s_bus == NULL) {
        return STARK_ERR_STATE;
    }
    i2c_master_dev_handle_t dev = device(addr);
    if (dev == NULL) {
        return STARK_ERR_IO;
    }
    return map(i2c_master_transmit_receive(dev, &off, 1, buf, len, (int)timeout_ms));
}
