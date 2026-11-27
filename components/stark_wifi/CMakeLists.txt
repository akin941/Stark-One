# Wi-Fi analysis (STARK-1001…1005, ADR-0027/0028). The dissectors, the survey and
# the analysis step (dot11_frame.c, survey.c, wifi_svc.c) are pure, host-tested
# cores; stark_wifi.c binds them to the esp_wifi transport and FreeRTOS and runs
# the service task (target-only). L3 — the pure cores stay host-testable.
idf_component_register(
    SRCS
        "dot11_frame.c"
        "survey.c"
        "wifi_svc.c"
        "stark_wifi.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        stark_event
        stark_log
        stark_wifi_transport
        freertos
)
