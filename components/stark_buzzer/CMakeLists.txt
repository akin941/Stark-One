# STARK-0020: tone sequencer on LEDC channel 0 (via stark_hal) + UI feedback sounds.
idf_component_register(
    SRCS
        "buzzer.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        stark_board
        stark_hal
        esp_timer
        freertos
)
