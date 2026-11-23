# Sub-GHz (STARK-0602): an RSSI meter and a band scan, receive only (ADR-0022).
idf_component_register(SRCS "app_subghz.c" "subghz_view.c"
    REQUIRES stark_app stark_ui stark_event stark_input stark_subghz stark_module
    WHOLE_ARCHIVE)
