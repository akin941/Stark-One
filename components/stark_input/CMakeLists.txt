# input_core.c: the pure key state machine (STARK-0011) — host-tested.
# input_sample.c: key GPIO levels -> raw bitmap via stark_hal — host-tested.
# input_service.c: the esp_timer sampling service (STARK-0012) — target-only.
idf_component_register(
    SRCS
        "input_core.c"
        "input_sample.c"
        "input_service.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_board
    PRIV_REQUIRES
        stark_event
        stark_hal
        stark_log
        esp_timer
)
