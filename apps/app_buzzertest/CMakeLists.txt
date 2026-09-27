idf_component_register(
    SRCS "app_buzzertest.c"
    REQUIRES stark_app stark_ui stark_buzzer stark_log
    WHOLE_ARCHIVE)
