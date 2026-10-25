# NFC (STARK-0404/0405): tag view, NDEF viewer, confirmed NDEF write, dumps —
# own tags only (ADR-0020).
idf_component_register(SRCS "app_nfc.c" "nfc_view.c" "nfc_write.c" "nfc_save.c"
    REQUIRES stark_app stark_ui stark_event stark_input stark_nfc stark_storage stark_log
    WHOLE_ARCHIVE)
