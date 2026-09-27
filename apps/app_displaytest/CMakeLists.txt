idf_component_register(
    SRCS "app_displaytest.c"
    REQUIRES stark_app stark_ui stark_gfx stark_hal stark_log
    WHOLE_ARCHIVE)
