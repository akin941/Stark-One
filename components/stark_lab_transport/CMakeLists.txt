# The lab tools' wires (STARK-0701, ADR-0023 §2): pads through stark_hal today. The
# emulator test image substitutes this component with one built on the test-only
# fake pads (test/emu/storage_image/components/). Layer 3 in scripts/check_layers.py:
# the pin guard reads stark_module's table (and the SPI group will use its lease), so
# it cannot sit below it.
idf_component_register(
    SRCS
        "lab_transport_hal.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
        stark_board
        stark_hal
        stark_module
)
