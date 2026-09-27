# ILI9341 bring-up (STARK-0015) and band rendering (STARK-0016).
# espressif/esp_lcd_ili9341 comes from this component's idf_component.yml
# (exact pin, ADR-0004). display_bands.c is pure and host-tested.
idf_component_register(
    SRCS
        "display_panel.c"
        "display_render.c"
        "display_bands.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
        stark_gfx
    PRIV_REQUIRES
        stark_board
        stark_hal
        stark_log
        esp_lcd
        heap
        freertos
)
