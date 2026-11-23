# Sub-GHz (STARK-0602…0605): an RSSI meter, a band scan, raw capture with its timing
# histogram, the saved captures and their gated replay (ADR-0022).
idf_component_register(SRCS "app_subghz.c" "subghz_view.c" "subghz_files.c" "subghz_send.c"
    REQUIRES stark_app stark_ui stark_event stark_input stark_subghz stark_module
             stark_storage stark_log
    WHOLE_ARCHIVE)
