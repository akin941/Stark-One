# The storage medium port (STARK-0201, ADR-0018 §2): microSD over the shared
# SPI2 bus. L1 — a hardware port, like stark_hal. Tests substitute this
# component as a whole (test/host/support/fake_medium.c,
# test/emu/storage_image/components/stark_storage_medium); nothing here
# knows about them.
idf_component_register(
    SRCS
        "medium_sdspi.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        stark_board
        log
        fatfs
        sdmmc
        esp_driver_sdspi
)
