idf_component_register(
    SRCS "app_about.c"
    REQUIRES stark_app stark_ui stark_gfx stark_event stark_board stark_log
             esp_app_format esp_hw_support spi_flash esp_timer heap
    WHOLE_ARCHIVE)
