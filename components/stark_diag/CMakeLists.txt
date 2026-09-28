# Diagnostics v1 (STARK-0109): diag_core.c is pure and host-tested;
# stark_diag.c is the esp_timer sampler (target-only). L3.
idf_component_register(
    SRCS
        "diag_core.c"
        "stark_diag.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        stark_event
        stark_hal
        stark_log
        esp_timer
        heap
        freertos
)
