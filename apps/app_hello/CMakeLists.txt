# The reference app (ARCHITECTURE §11): the whole extension recipe in one
# directory plus one registry line in main/ (ADR-0016).
idf_component_register(SRCS "app_hello.c"
    REQUIRES stark_app stark_ui stark_gfx stark_event stark_log esp_timer
    WHOLE_ARCHIVE)
