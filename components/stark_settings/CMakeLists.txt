# Typed settings over NVS (STARK-0204): settings_core.c is pure and
# host-tested; stark_settings.c is the NVS port (target-only). L3.
idf_component_register(
    SRCS
        "settings_core.c"
        "stark_settings.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        stark_event
        stark_log
        nvs_flash
)
