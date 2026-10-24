# Infrared (STARK-0301…0307, ADR-0019). The codec core is pure and
# host-tested; the RMT port arrives with STARK-0303. L3.
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
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
)
