# STARK-0009: the pure core only. The FreeRTOS port (stark_event.c, the
# global bus) is added by STARK-0010.
idf_component_register(
    SRCS
        "stark_event_core.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
)
