# STARK-0015: ILI9341 bring-up. espressif/esp_lcd_ili9341 comes from this
# component's idf_component.yml (exact pin, ADR-0004).
idf_component_register(
    SRCS
        "display_panel.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        stark_board
        stark_gfx
        stark_hal
        esp_lcd
        heap
        freertos
)
