# NFC (STARK-0401…0405, ADR-0020). The NFC core is pure and host-tested;
# stark_nfc.c binds it to the I2C transport and FreeRTOS (target-only). L3.
idf_component_register(
    SRCS
        "ndef.c"
        "pn532_frame.c"
        "pn532.c"
        "nfc_ident.c"
        "nfc_type2.c"
        "nfc_reader.c"
        "nfc_poll.c"
        "stark_nfc.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        stark_event
        stark_log
        stark_nfc_transport
        esp_timer
        freertos
)
