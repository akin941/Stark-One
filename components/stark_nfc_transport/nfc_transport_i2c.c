/*
 * nfc_transport_i2c.c — the PN532 over ESP-IDF's I2C master driver
 * (stark_nfc_transport.h). Target-only.
 */
#include "driver/i2c_master.h"
#include "sdkconfig.h"
#include "stark_board.h"
#include "stark_nfc_transport.h"

#define PN532_I2C_ADDR 0x24 /* UM0701-02 §6.2.4: 0x48 write / 0x49 read */

static i2c_master_dev_handle_t s_dev;

static stark_err_t map(esp_err_t err)
{
    switch (err) {
        case ESP_OK:
            return STARK_OK;
        case ESP_ERR_INVALID_RESPONSE: /* the address was not acknowledged */
            return STARK_ERR_NOT_FOUND;
        case ESP_ERR_TIMEOUT:
            return STARK_ERR_TIMEOUT;
        default:
            return STARK_ERR_IO;
    }
}

stark_err_t stark_nfc_transport_open(void)
{
    if (s_dev != NULL) {
        return STARK_OK;
    }
    i2c_master_bus_handle_t bus = stark_board_i2c_bus();
    if (bus == NULL) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PN532_I2C_ADDR,
        .scl_speed_hz = CONFIG_STARK_NFC_I2C_HZ,
    };
    return map(i2c_master_bus_add_device(bus, &cfg, &s_dev));
}

void stark_nfc_transport_close(void)
{
    if (s_dev != NULL) {
        (void)i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
}

stark_err_t stark_nfc_transport_write(const uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    if (s_dev == NULL) {
        return STARK_ERR_STATE;
    }
    return map(i2c_master_transmit(s_dev, buf, len, (int)timeout_ms));
}

stark_err_t stark_nfc_transport_read(uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    if (s_dev == NULL) {
        return STARK_ERR_STATE;
    }
    return map(i2c_master_receive(s_dev, buf, len, (int)timeout_ms));
}
