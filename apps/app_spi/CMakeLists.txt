# SPI (STARK-0706, ADR-0023 §6): one bounded, confirmed SPI transaction on a module slot whose
# module declares SPI and no function, through stark_lab; a slot owned by a driver is refused.
# spi_views.c is pure (host-tested).
idf_component_register(SRCS "app_spi.c" "spi_views.c"
    REQUIRES stark_app stark_ui stark_event stark_input stark_lab stark_log stark_module
    WHOLE_ARCHIVE)
