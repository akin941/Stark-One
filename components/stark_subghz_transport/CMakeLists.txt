# The CC1101 module's wires (STARK-0601, ADR-0022 §3): SPI through the claimed
# slot and GDO2 through RMT. The emulator test image substitutes this component
# with one built on the test-only fake CC1101 (test/emu/storage_image/
# components/). Layer 3 in scripts/check_layers.py: it uses stark_module's lease
# and SPI device, so it cannot sit below it.
idf_component_register(
    SRCS
        "subghz_transport_hw.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
        stark_module
    PRIV_REQUIRES
        esp_driver_rmt
        freertos
)
