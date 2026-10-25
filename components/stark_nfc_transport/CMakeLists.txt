# The NFC transport (STARK-0402, ADR-0020 §2): the PN532 at 0x24 on the
# board's I2C bus. The emulator test image substitutes this component with
# one built on the test-only fake PN532 (test/emu/storage_image/components/).
# L1.
idf_component_register(
    SRCS
        "nfc_transport_i2c.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        stark_board
        esp_driver_i2c
)
