# STARK-0017: screen stack, status bar, damage-driven rendering, UI loop.
idf_component_register(
    SRCS
        "ui_stack.c"
        "ui_statusbar.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
        stark_event
        stark_gfx
    PRIV_REQUIRES
        stark_display
        stark_hal
        stark_input
        stark_log
)
