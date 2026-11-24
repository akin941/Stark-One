# Lab service (STARK-0701…0707, ADR-0023). The pad decisions (lab_pins.c), the PWM and pulse
# rules (lab_pwm.c), the frequency and duty arithmetic (lab_meas.c), the I2C rules and hints
# (lab_i2c.c, lab_i2c_hints.c), the UART tool's rules, auto-baud and session (lab_uart.c,
# lab_autobaud.c, lab_uart_svc.c) and the SPI probe's rules (lab_spi.c) are pure and host-tested;
# stark_lab.c binds them to stark_module's table, stark_lab_transport and FreeRTOS (target-only). L3.
idf_component_register(
    SRCS
        "lab_autobaud.c"
        "lab_i2c.c"
        "lab_i2c_hints.c"
        "lab_meas.c"
        "lab_pins.c"
        "lab_pwm.c"
        "lab_spi.c"
        "lab_uart.c"
        "lab_uart_svc.c"
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
