# Storage (STARK-0202/0203): the card's state, Eject, Mount and the write test.
idf_component_register(SRCS "app_storage.c" "storage_wtest.c"
    REQUIRES stark_app stark_ui stark_event stark_storage stark_log
    WHOLE_ARCHIVE)
