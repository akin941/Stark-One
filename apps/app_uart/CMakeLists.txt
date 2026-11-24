# UART (STARK-0705, ADR-0023 §7): a hex/ASCII monitor, bounded sends, a loopback test, auto-baud and
# a card log on the two lab pins through stark_lab; 3.3 V TTL only. uart_views.c is pure
# (host-tested); uart_log.c talks to stark_storage only.
idf_component_register(SRCS "app_uart.c" "uart_views.c" "uart_log.c"
    REQUIRES stark_app stark_ui stark_gfx stark_event stark_input stark_lab stark_log stark_hal
             stark_storage
    WHOLE_ARCHIVE)
