# STARK-0011: the pure key state machine only. The sampling service
# (input_service.c, esp_timer + stark_hal + stark_event) is STARK-0012's.
idf_component_register(
    SRCS
        "input_core.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_board
)
