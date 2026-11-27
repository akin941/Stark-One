# Wi-Fi analysis (STARK-1001…, ADR-0027). The dissectors are a pure, host-tested
# core; the esp_wifi promiscuous transport and the service task arrive at
# STARK-1004 (a separate component, the only esp_wifi caller). L2 — no ESP-IDF.
idf_component_register(
    SRCS
        "dot11_frame.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
)
