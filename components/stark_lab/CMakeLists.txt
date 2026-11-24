# Lab service (STARK-0701…0707, ADR-0023). The pad decisions (lab_pins.c) and the PWM and pulse
# rules (lab_pwm.c) are pure and host-tested; stark_lab.c binds them to stark_module's table, stark_lab_transport and
# FreeRTOS (target-only). L3.
idf_component_register(
    SRCS
        "lab_pins.c"
        "lab_pwm.c"
        "stark_lab.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
        stark_board
    PRIV_REQUIRES
        stark_event
        stark_lab_transport
        stark_log
        stark_module
        esp_rom
        freertos
)
