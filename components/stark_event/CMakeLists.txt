# stark_event_core.c: the pure core (STARK-0009) — host-tested, no OS.
# stark_event.c: the global bus, its FreeRTOS port (STARK-0010) — target-only.
idf_component_register(
    SRCS
        "stark_event_core.c"
        "stark_event.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        stark_hal
        freertos
)
