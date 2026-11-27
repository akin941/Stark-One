# BLE analysis (STARK-1002…, ADR-0027). The advertising-PDU dissector is a pure,
# host-tested core; the BLE-controller observer transport and the service task
# arrive at STARK-1005 (a separate component, the only BLE-controller caller).
# L2 — no ESP-IDF.
idf_component_register(
    SRCS
        "ble_adv.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
)
