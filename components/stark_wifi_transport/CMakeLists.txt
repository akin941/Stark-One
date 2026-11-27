# Wi-Fi transport (STARK-1005, ADR-0027/0028): the only esp_wifi caller, promiscuous
# receive only. L1 hardware port. The emulator test image substitutes this component
# with test/emu/storage_image/components/stark_wifi_transport (a fake); the production
# image builds this file but the service opens it lazily, never at boot (ADR-0028).
idf_component_register(
    SRCS
        "wifi_transport.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        esp_wifi
        esp_event
        freertos
)
