# Infrared (STARK-0301…0307, ADR-0019). The codec core is pure and
# host-tested; stark_ir.c is the RMT port (target-only). L3.
idf_component_register(
    SRCS
        "ir_timing.c"
        "ir_nec.c"
        "ir_codec.c"
        "ir_biphase.c"
        "ir_rc5.c"
        "ir_rc6.c"
        "ir_sirc.c"
        "ir_raw.c"
        "ir_symbols.c"
        "ir_capture.c"
        "ir_tx_guard.c"
        "stark_ir.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        stark_board
        stark_event
        stark_log
        esp_driver_rmt
        esp_timer
        freertos
)
