# Settings (STARK-0205): the four settings, live, committed on exit.
idf_component_register(SRCS "app_settings.c"
    REQUIRES stark_app stark_ui stark_settings
    WHOLE_ARCHIVE)
