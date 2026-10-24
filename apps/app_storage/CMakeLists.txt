# Storage (STARK-0202): the card's state, Eject and Mount.
idf_component_register(SRCS "app_storage.c"
    REQUIRES stark_app stark_ui stark_event stark_storage stark_log
    WHOLE_ARCHIVE)
