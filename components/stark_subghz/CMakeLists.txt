# Sub-GHz (STARK-0601…0605, ADR-0022). The register profiles, the CC1101 driver
# and the module service are pure and host-tested; stark_subghz.c binds them to
# the claimed slot, stark_subghz_transport and FreeRTOS (target-only). L3.
idf_component_register(
    SRCS
        "cc1101_regs.c"
        "cc1101_drv.c"
        "subghz_rssi.c"
        "subghz_raw.c"
        "subghz_decode.c"
        "subghz_txpolicy.c"
        "subghz_svc.c"
        "stark_subghz.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
        stark_subghz_transport
    PRIV_REQUIRES
        stark_event
        stark_log
        stark_module
        esp_timer
        freertos
)
