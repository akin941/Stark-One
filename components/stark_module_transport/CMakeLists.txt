# The module ID EEPROM transport (STARK-0502, ADR-0021 §6): read-only access to
# the 24C02s on the board's I2C bus. The emulator test image substitutes this
# component with one built on the test-only fake (test/emu/storage_image/
# components/). L1.
idf_component_register(
    SRCS
        "module_transport_i2c.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        stark_board
        esp_driver_i2c
)
