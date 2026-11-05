# Module bus (STARK-0501…0505, ADR-0021). The descriptor parser and discovery
# core are pure and host-tested; stark_module.c binds them to the transport and
# FreeRTOS (target-only). L3.
idf_component_register(
    SRCS
        "module_desc.c"
        "module_core.c"
        "stark_module.c"
        "stark_module_spi.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        stark_event
        stark_board
        stark_log
        esp_driver_spi
        stark_module_transport
        freertos
)
